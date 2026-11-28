# Auditoría de Aplicaciones DSM — Entrega Final

**Agente:** AGENTE 7 — Aplicaciones, procesos distribuidos y funcionalidades  
**Fecha:** 27 de noviembre de 2026  
**Repositorio:** `/home/sebastianaguero/Documents/GitHub/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida/Entrega_Final/codigo`

---

## Resumen Ejecutivo

Se auditan **9 aplicaciones** (`demo`, `matrices`, `file`, `counter`, `bench`, `evict`, `chat`, `task`, `stress`) implementadas en `Entrega_Final/codigo/src/app_*.c` + `demo_page.c`, con utilidades comunes en `app_common.c` y despachador en `apps.c`. Todas siguen el modelo **SPMD**: mismos `dsm_malloc` en el mismo orden, sincronización con `dsm_barrier(rt, rt->cfg.barrier_timeout_ms)`.

**Veredicto general:** Las aplicaciones están bien diseñadas para validar el DSM (coherencia MSI, paginación bajo demanda, expulsión LRU, barreras). Existen **bugs conocidos documentados** (instabilidad de `evict` con 4 nodos/512 páginas, pérdida de incrementos en `counter` bajo alta contención, SEGFAULT en `bench` nodo 1 por `DSM_ERR_NOENT`, thrashing en `task` con 1M elementos). No se modificó código; solo se audita y reporta.

---

## 1. demo (`demo_page.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Demostración mínima de una página compartida entre 2+ nodos (E2). Secuencia: nodo 0 escribe → nodo 1 lee (I→S) → nodo 1 escribe (S→M, invalida a nodo 0) → nodo 0 vuelve a leer (home reenvía desde dueño). |
| **Parámetros** | Ninguno (usa `--app demo`). Tamaño fijo: 1 página (4 KB). |
| **Barreras** | **3 barreras** centralizadas con `demo_barrier()` que usa `rt->cfg.barrier_timeout_ms` (fallback 120 000 ms). Nodos ≥2 solo esperan barreras. |
| **Verificación de corrección** | Nodo 0 imprime stats finales: `fallos`, `lecturas_remotas`, `escrituras_remotas`, `invalidaciones_enviadas`, `reenvios`. |
| **Resultado esperado (docs §15.3)** | `OK (fallos 2/2, reenvios 1, barreras 3, errores 0)` ~2 s. |
| **Evidencia código** | `demo_page.c:34-47` (barrera con timeout configurable), `demo_page.c:76-99` (secuencia de 3 barreras), `demo_page.c:101-113` (impresión de stats). |
| **Bugs conocidos** | Ninguno reportado en docs. Requiere exactamente 2+ nodos; nodos extra solo esperan. |

---

## 2. matrices (`app_matrices.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Multiplicación de matrices por bloques de filas: `C = A × B`. Nodo 0 inicializa A y B; todos leen A/B (copias S) y cada nodo escribe su bloque de C (copias M). Verificación determinista recalcula y compara. |
| **Parámetros** | `--size N` (lado de matriz, default 64). Tamaños probados: **64, 128, 192**. `--no-verify` omite verificación. |
| **Barreras** | 2 barreras: tras inicialización de A/B y tras cálculo de C. Usan `rt->cfg.barrier_timeout_ms`. |
| **Verificación numérica** | Doble verificación (líneas 97-137):<br>1. **A y B** contra fórmulas deterministas `(i+j)%7+1` y `(i*j)%5+1` (detecta A/B obsoletas).<br>2. **C** recalculando bloque propio y comparando con `fabs(c - sum) > 1e-9`. |
| **Resultado esperado** | **0 discrepancias**. Docs §15.3: `matrices 64` OK (0 discrepancias, fallos 20/20, errores 0) ~60 s. `matrices 192` pendiente. 4 nodos: `matrices 64 (4n)` OK (0 discrepancias ×4, fallos 18 c/u). |
| **Evidencia código** | `app_matrices.c:26` (default size 64), `app_matrices.c:56-62` (inicialización determinista), `app_matrices.c:70-88` (barrera 1), `app_matrices.c:97-137` (verificación doble), `app_matrices.c:63,85` (barreras con `rt->cfg.barrier_timeout_ms`). |
| **Bugs conocidos** | `matrices 192` no ejecutado en corrida final (§15.3). Speedup sublineal documentado (§16.3): 2 nodos = 0.02x, 4 nodos = 0.03x por overhead de coherencia. |

---

## 3. file (`app_file.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Checksum distribuido de archivo (o patrón sintético 1 MB). Nodo 0 carga archivo/patrón; todos leen tramo distinto y calculan checksum parcial (una página por nodo, **sin falsos compartidos**); nodo 0 verifica recalculando cada bloque sobre la copia compartida y comparando con lo que escribió su dueño. |
| **Parámetros** | `--size N` (bytes, default **1 MB = 1024×1024**). `--file RUTA` para archivo real. `--no-verify` omite verificación. |
| **Barreras** | 2 barreras: tras carga del archivo y tras checksums parciales. Usan `rt->cfg.barrier_timeout_ms`. |
| **Checksum** | FNV-1a 32 bits (`dsm_app_checksum`, `app_common.c:57-71`). Patrón sintético: `buf[i] = (i*31+7)&0xFF`. Checksum completo esperado: **`0xc2dc9dc5`**. |
| **Verificación** | Nodo 0 recalcula cada bloque sobre buffer compartido y compara con partial written por cada nodo (líneas 117-125). No suma hashes parciales (incorrecto); verifica datos + coherencia del array de parciales. |
| **Resultado esperado (docs §14.9, §15.3)** | `checksum completo = 0xc2dc9dc5, OK` (2 bloques, fallos 262/130, errores 0) ~60 s. Archivo inexistente → `abortado (archivo no disponible)`, exit ≠ 0. |
| **Evidencia código** | `app_file.c:40` (default 1 MB), `app_file.c:73-77` (patrón sintético), `app_file.c:93` (checksum parcial), `app_file.c:107-128` (verificación nodo 0), `app_file.c:126` (imprime `0x%08x`). |
| **Bugs conocidos** | Si archivo no existe, nodo 0 marca `partial[0]=1` y propaga abort (líneas 62, 86-89). |

---

## 4. counter (`app_counter.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Incrementos concurrentes con **falsos compartidos** intencionales. Cada hilo incrementa su ranura (8 bytes) y lee la ranura del vecino (`neighbor_slot`), forzando invalidaciones: mientras vecino tiene página en M, la lectura la baja a S y el siguiente incremento del vecino recupera propiedad. Patrón clásico para ver contador de invalidaciones moverse. |
| **Parámetros** | `--iters N` (default **100**), `--threads N` (default **2**, máx 16). Una página por nodo (256 ranuras de 8 bytes c/u). |
| **Barreras** | 1 barrera tras incrementos. Usa `rt->cfg.barrier_timeout_ms`. |
| **Verificación** | Nodo 0 suma todos los contadores y compara contra `esperado = nodos × hilos × iters`. Con 2 nodos × 2 hilos × 100 iters = **400**. Imprime `suma = X, esperado = Y, OK/FALLO`. |
| **Resultado esperado (docs §15.3)** | `OK (suma 400 = esperado, fallos 4/3, errores 0)` ~2 s. |
| **Evidencia código** | `app_counter.c:62-63` (defaults), `app_counter.c:90-95` (stride = page_size/8, slot por hilo, neighbor_slot = nodo siguiente), `app_counter.c:48-51` (worker: `counters[slot]++` + `sink += counters[neighbor_slot]`), `app_counter.c:121-137` (verificación). |
| **Bugs conocidos (docs §4.3.2, §16.3)** | **Pérdida de incrementos con contención alta** (documentado como limitación conocida). Falsos compartidos por diseño: 1 página por nodo, pero hilos en misma página causan ping-pong de ownership. No hay pérdida en configuración base (2 nodos, 2 hilos, 100 iters). |

---

## 5. evict (`app_evict.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Presión de memoria para forzar expulsión LRU y swap. Reserva más páginas que marcos (default **512 páginas**, 256 marcos = 1 MB/nodo). Primera pasada: cada nodo escribe solo sus páginas (`p % nodes == rank`). Segunda pasada: **todos leen todas** → resolutor expulsa (swap en home, liberación en clientes) y vuelve a traer. Verifica byte escrito por dueño sobreviva ida y vuelta. |
| **Parámetros** | `--pages N` (default **512**, máx 8192, mín 8). `--frames M` (vía `--frames` en CLI, sobrescribe `node_memory`). |
| **Barreras** | 2 barreras: tras primera pasada (escritura) y tras segunda pasada (lectura/verificación). Usan `rt->cfg.barrier_timeout_ms`. |
| **Verificación** | Segunda pasada (líneas 77-94): recorre todas las páginas, `expected = (i + (i % nodes)) & 0xFF`. Cuenta `bad` y falla si >0. |
| **Stats impresos** | `expulsiones`, `sucias`, `swap bytes`, `residentes`, `libres` (líneas 101-110). |
| **Resultado esperado (docs §15.3, §14.9)** | `OK (expulsiones 64, residentes 32, suma 2048, errores 0)` ~60 s. Configuración de prueba: **64 páginas / 32 marcos** (S7 en §15.1). |
| **Evidencia código** | `app_evict.c:30` (default 512 páginas), `app_evict.c:62-64` (primera pasada disjunta), `app_evict.c:80-86` (verificación `expected = (i + (i%nodes))&0xFF`), `app_evict.c:101-110` (stats). |
| **Bugs conocidos (docs §4.3.2, §15.3)** | **Inestabilidad de `evict` con 4 nodos y 512 páginas** (documentado como limitación). Prueba S7 usa 2 nodos, 64 páginas / 32 marcos. Con 4 nodos y 512 páginas hay thrashing excesivo. |

---

## 6. bench (`app_bench.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Latencia y throughput de páginas: **lectura local** (acierto, sin red), **lectura remota** (PAGE_REQUEST + PAGE_FORWARD), **escritura remota** (OWNERSHIP_REQUEST + INVALIDATE + GRANT). Para que cada op remota sea realmente remota, nodo `r` usa páginas que inicialmente pertenecen a `(r+1)%n` y no las vuelve a tocar. |
| **Parámetros** | `--pages N` (default 64), `--iters N` (default `pages/nodes`). 3 fases separadas. |
| **Barreras** | 2 barreras: tras distribución inicial y tras 3 fases. Usan `rt->cfg.barrier_timeout_ms`. |
| **Métricas** | `print_rate()` imprime: ops, us/op, MB/s por fase y nodo. |
| **Resultados medidos (docs §16.2)** | Loopback 2 nodos:<br>- Nodo 0: local 0.03 µs/op, remoto 64.31 µs/op, escritura 42.09 µs/op<br>- Nodo 1: local 0.00 µs/op, remoto 64.00 µs/op, escritura 60.59 µs/op<br>Throughput: lectura remota ~60-61 MB/s, escritura remota ~64-93 MB/s. Latencia RPC media 40-41 µs. |
| **Evidencia código** | `app_bench.c:77-81` (reparto inicial: nodo r escribe páginas `p % nodes == r`), `app_bench.c:90-98` (fase 1 local), `app_bench.c:100-109` (fase 2 remota: `(rank+1)%nodes`), `app_bench.c:111-120` (fase 3 escritura remota). |
| **Bugs conocidos — SEGFAULT en nodo 1 (`DSM_ERR_NOENT`)** | El código de `bench` accede a páginas del nodo `(rank+1)%nodes` en fases 2 y 3. Si el home no tiene la página materializada (p.ej., página nunca escrita por el owner original, o expulsada y swap falla), `coherence.c` retorna `DSM_ERR_NOENT` (líneas 548, 564, 592). El handler de `SIGSEGV` recibe el error y mata el hilo → **SEGFAULT aparente en nodo 1**. Ocurre cuando: (a) página no inicializada por owner, (b) swap deshabilitado/fallido, (c) `dsm_memory_page_lookup` retorna NULL y `obj == DSM_INVALID_OBJECT`. Ver `coherence.c:548,564,592`. |

---

## 7. chat (`app_chat.c`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Chat de texto libre interactivo entre nodos. Cada nodo escribe en su propia página (1 página/nodo, seqlock con `seq` atómico + `magic` + `checksum` FNV-1a). Tras escribir, llama `dsm_release()` para que home instale copia y avise a lectores. Lectores sondean (`poll` 200 ms) y muestran mensajes nuevos validando `magic`, `seq`, `checksum`, `len`. |
| **Parámetros** | `--nick NOMBRE` (default `nodo<N>`), `--text TEXTO` (mensaje inicial), `--duration S` (auto-salida), `--serve` (queda escuchando). Requiere **≥2 nodos**. |
| **Barreras** | 1 barrera inicial + 1 barrera final best-effort (no bloquea si alguien salió antes). Usan `rt->cfg.barrier_timeout_ms`. |
| **Sincronización** | Seqlock: escritor incrementa `seq` (release), lector carga `seq` (acquire), copia, revalida `seq`. Si cambió, descarta. Checksum FNV-1a del texto valida integridad. |
| **Comandos** | `/quit`, `/nick NOMBRE`, `/help`. Rechaza caracteres de control (<0x20 salvo tab). |
| **Resultado esperado (docs §15.3)** | `OK (mensajes cruzan: [nodo 1] bob: hola desde bob)` ~8 s. |
| **Evidencia código** | `app_chat.c:33-42` (struct `chat_slot_t` 4096 B), `app_chat.c:61-88` (`print_remote` con validación seqlock+checksum), `app_chat.c:90-113` (`send_msg` + `dsm_release`), `app_chat.c:195-290` (loop principal con `poll`). |
| **Bugs conocidos** | `chat` no se prueba automatizado (interactivo, §15.1). Corrección: stdin cerrado no entra en bucle (líneas 218-235). |

---

## 8. task (`app_task.c`) — Extra 1

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Ejecución distribuida de una tarea (Extra 1). Nodo 0 (coordinador) prepara array de `N` doubles; todos los nodos procesan su porción (suma de cuadrados `input[i]*input[i]`) y publican resultado parcial en ranura propia (1 página/nodo, **sin falsos compartidos**); coordinador recolecta, verifica contra referencia secuencial, reporta **speedup** y **balance de carga**. |
| **Parámetros** | `--size N` (default **1 000 000** elementos). Requiere **≥2 nodos**. |
| **Barreras** | 3 barreras: tras preparación, tras cómputo, final. Usan `rt->cfg.barrier_timeout_ms`. |
| **Métricas** | `speedup = tiempo_secuencial / max(tiempo_paralelo_por_nodo)`, `balance = max_time / min_time`. Overhead secuencial = `tseq1 - tseq0`. |
| **Resultados medidos (docs §17.2)** | - **≤100K elementos**: ejecución distribuida real, **speedup 0.02–0.03x**, **balance 109–135x** (desequilibrio extremo: nodo 0 hace trabajo secuencial + coordinación).<br>- **1M elementos**: **falla por thrashing** (conjunto de trabajo > marcos, expulsión LRU constante, swap satura). |
| **Evidencia código** | `app_task.c:29` (default 1M), `app_task.c:43-44` (toma `opts->size`), `app_task.c:102-103` (`dsm_app_split_start/count`), `app_task.c:106-122` (cómputo + publicación), `app_task.c:130-161` (coordinador: verificación, speedup, balance). |
| **Bugs conocidos (docs §17.2, §4.3.2)** | **Thrashing con 1M elementos** (falla). Speedup sublineal por overhead de coherencia (invalidaciones, transferencias, barreras) y contención de red en loopback. Balance 109-135x indica distribución muy desigual (coordinador hace trabajo secuencial de referencia). |

---

## 9. stress (combinación en `apps.c` / `dsm.sh`)

| Aspecto | Detalle |
|---------|---------|
| **Propósito** | Combina `matrices 192` + `counter 100×2` para probar sistema bajo carga combinada. |
| **Estado** | **No ejecutado en corrida final** (§15.3: S9 pendiente). |
| **Evidencia** | `run_acceptance.sh` no lo incluye. `dsm.sh` menú opción lo menciona. |

---

## 10. Barreras: Uso unificado de `rt->cfg.barrier_timeout_ms`

Todas las aplicaciones usan la **barrera centralizada** con timeout configurable:

| App | Barreras | Timeout usado |
|-----|----------|---------------|
| demo | 3 | `rt->cfg.barrier_timeout_ms` (fallback 120 000 ms) |
| matrices | 2 | `rt->cfg.barrier_timeout_ms` |
| file | 2 | `rt->cfg.barrier_timeout_ms` |
| counter | 1 | `rt->cfg.barrier_timeout_ms` |
| bench | 2 | `rt->cfg.barrier_timeout_ms` |
| evict | 2 | `rt->cfg.barrier_timeout_ms` |
| chat | 1 + 1 best-effort | `rt->cfg.barrier_timeout_ms` |
| task | 3 | `rt->cfg.barrier_timeout_ms` |

**Evidencia:** `demo_page.c:36`, `app_matrices.c:63,85`, `app_file.c:80,98`, `app_counter.c:113`, `app_bench.c:82,122`, `app_evict.c:65,95`, `app_chat.c:173,293`, `app_task.c:87,123,172`. Configuración en `dsm_config.h` → `barrier_timeout_ms` (default 30 000 ms).

---

## 11. Patrones de Sincronización y Coherencia

| Patrón | Apps | Descripción |
|--------|------|-------------|
| **SPMD + dsm_malloc ordenado** | Todas | Mismos `dsm_malloc` en mismo orden → mismo `page_base` global. Garantiza que todos los nodos ven los mismos objetos. |
| **Barrera centralizada** | Todas | Sincroniza fases; home (nodo 0) es árbitro. Timeout configurable evita bloqueo infinito. |
| **Una página por nodo (sin falsos compartidos)** | file, counter, evict, task, chat | `dsm_malloc(nodes * page_size)` → cada nodo escribe en `rank * page_size`. Elimina ping-pong de ownership por falsos compartidos. |
| **Falsos compartidos intencionales** | counter | Hilos leen `neighbor_slot` en página ajena → invalidaciones cruzadas. Mide contador de invalidaciones. |
| **Lectura remota forzada** | bench, matrices | Nodo `r` accede a páginas de `(r+1)%nodes` → primera lectura = PAGE_REQUEST + PAGE_FORWARD. |
| **Escritura remota forzada** | bench | Fase 3 escribe en páginas de `(r+1)%nodes` → OWNERSHIP_REQUEST + INVALIDATE + GRANT. |
| **Verificación determinista** | matrices, file, counter, task | Recalculan localmente y comparan contra estado compartido. Detectan incoherencia de protocolo. |
| **Seqlock + checksum** | chat | Escritor: `seq++` (release) + `dsm_release`. Lector: carga `seq` (acquire), copia, revalida `seq`, verifica checksum. |
| **Expulsión LRU + swap** | evict | Escribe > marcos → `coherence_make_room` → `pick_victim` → swap_out (home) / PAGE_RELEASE (clientes). Segunda pasada trae de vuelta. |

---

## 12. Bugs Conocidos por Aplicación (Resumen)

| App | Bug / Limitación | Evidencia |
|-----|------------------|-----------|
| **demo** | Ninguno reportado | — |
| **matrices** | `matrices 192` no ejecutado; speedup sublineal (0.02-0.03x) | §15.3, §16.3 |
| **file** | Archivo inexistente → abort propagado (comportamiento correcto) | `app_file.c:62,86-89` |
| **counter** | **Pérdida de incrementos con contención alta** (documentado) | §4.3.2, §16.3 |
| **bench** | **SEGFAULT en nodo 1 por `DSM_ERR_NOENT`** al acceder páginas remotas no materializadas / swap fallido | `coherence.c:548,564,592` |
| **evict** | **Inestable con 4 nodos y 512 páginas** (thrashing excesivo) | §4.3.2, §15.3 (S7 usa 2 nodos, 64p/32m) |
| **chat** | No testeado automatizado (interactivo) | §15.1 |
| **task** | **Thrashing con 1M elementos** (falla); balance 109-135x (desequilibrio) | §17.2, §4.3.2 |
| **stress** | No ejecutado | §15.3 |

---

## 13. Evidencia de Tests Reales (Logs y Docs)

| Fuente | Contenido |
|--------|-----------|
| `docs/15_pruebas_ejecucion.md` §15.3 | Tabla de resultados medidos S1-S11 con tiempos, fallos, errores. |
| `docs/04_Entrega_Final.md` §16.2 | Latencias bench: nodo 0 local 0.03, remoto 64.31, escritura 42.09 µs/op. |
| `docs/04_Entrega_Final.md` §16.3 | Speedup matrices: 2 nodos 0.02x, 4 nodos 0.03x. Tiempos medidos. |
| `docs/04_Entrega_Final.md` §17.2 | Task: ≤100K speedup 0.02-0.03x, balance 109-135x; 1M falla thrashing. |
| `tests/run_acceptance.sh` | Script de aceptación: grep patterns para validar cada app (líneas 28-68). |
| `logs/dsm-node*.log` | Logs de bootstrap, heartbeat, RPC reintentos (PEER_DOWN, TIMEOUT). No contienen salida de apps (apps imprimen a stdout). |

**Nota:** Los logs en `codigo/logs/` muestran solo bootstrap/heartbeat/RPC; la salida de las aplicaciones va a stdout/stderr (capturada en `run_acceptance.sh` → `/tmp/accept_${app}_0.log`). No hay logs de ejecución de apps en el repo actual.

---

## 14. Conclusiones de la Auditoría

1. **Cobertura funcional completa:** Las 9 apps cubren todos los subsistemas del DSM (paginación bajo demanda, coherencia MSI, barreras, expulsión LRU, swap, red, estadísticas).

2. **Diseño correcto para validación:** Cada app tiene verificación determinista que detecta incoherencias del protocolo (matrices verifica A/B/C, file verifica checksums sobre datos compartidos, counter suma contadores, evict verifica byte patrón, task compara paralelo vs secuencial).

3. **Uso consistente de barreras:** Todas usan `rt->cfg.barrier_timeout_ms` unificado. Timeouts documentados (30s default, 120s demo).

4. **Bugs documentados y acotados:**
   - `bench` SEGFAULT (`DSM_ERR_NOENT`) es bug real en ruta de fallo de página remota.
   - `evict` inestabilidad con 4 nodos/512p es limitación conocida de presión de memoria.
   - `counter` pérdida de incrementos bajo alta contención es consecuencia de falsos compartidos + protocolo MSI sin coalescing.
   - `task` thrashing con 1M es esperado (working set > marcos).

5. **Extra 1 implementado y medido:** `task` cumple especificación (ejecución distribuida real ≤100K, speedup 0.02-0.03x, balance 109-135x, falla 1M por thrashing).

6. **No hay código modificado:** Esta auditoría es de solo lectura. Los hallazgos coinciden con los documentados en `04_Entrega_Final.md` y `15_pruebas_ejecucion.md`.

---

**Fin del informe de auditoría — AGENTE 7**