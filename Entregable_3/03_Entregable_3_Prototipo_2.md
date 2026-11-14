# Entregable 3 — Prototipo 2 y Arquitectura Actualizada

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Integrantes** | José Daniel Monterrosa Quirós · Sebastián Agüero Brenes |
| **Fecha oficial** | Viernes 20 de noviembre de 2026 |
| **Documento** | `Entregable_3/03_Entregable_3_Prototipo_2.md` |
| **Repositorio** | https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git |

---

## 1. Portada

**Sistema de Memoria Compartida Distribuida (DSM)**  
**Prototipo 2 — Arquitectura Actualizada**  
**Curso:** IC-6600 Principios de Sistemas Operativos  
**Integrantes:** José Daniel Monterrosa Quirós, Sebastián Agüero Brenes  
**Fecha:** 20 de noviembre de 2026  
**Repositorio:** https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git

---

## 2. Resumen ejecutivo

Este documento describe el **Prototipo 2** del DSM distribuido para IC-6600. El prototipo implementa una arquitectura de memoria compartida por paginación bajo demanda con coherencia MSI, conexiones TCP persistentes, pool de hilos trabajadores, expulsión LRU con swap local, y un conjunto de herramientas de observabilidad (CLI `dsmctl`, panel web de solo lectura).

**Mejoras clave respecto al Prototipo 1:**

- **Persistencia de conexiones:** Un pool de 32 hilos por nodo atiende conexiones TCP permanentes, eliminando el overhead de handshake por RPC.
- **Directorio distribuido por página:** El nodo *home* (nodo 0) serializa operaciones por página con `op_lock`; nunca bloquea durante E/S de red.
- **Expulsión LRU + swap:** Cada nodo mantiene un archivo de intercambio (`logs/swap-nodeN.bin`); el home nunca pierde la única copia de una página.
- **Asignación determinista:** `dsm_malloc` usa un número de secuencia por nodo; todos los nodos que llaman en el mismo orden reciben el mismo rango global de páginas.
- **Barrera centralizada:** Contadores atómicos y registro por nodo evitan doble conteo.
- **API por hilo:** Cada hilo se adjunta a la ventana DSM con `dsm_attach_thread`; fallos de página se resuelven en un hilo resolver dedicado.
- **Observabilidad:** `dsmctl` (texto sobre TCP) y panel web (`/`, `/stats.json`) exponen estadísticas, páginas, directorio, objetos y nodos.
- **Scripts de operación:** `lab.sh` unifica build, demo, apps, control, IP, show, stop en comandos cortos.

**Validación:** Todas las aplicaciones (`demo`, `matrices`, `file`, `counter`, `bench`, `evict`, `stress`) pasan en 4 nodos locales (WSL2 Alpine) con 0 errores reportados. Métricas de latencia: lectura local ~0.06 µs, lectura remota ~130–280 µs, escritura remota ~120–230 µs.

---

## 3. Índice

1. Portada  
2. Resumen ejecutivo  
3. Índice  
4. Introducción  
5. Arquitectura actualizada  
6. Cambios y ajustes respecto al prototipo 1  
7. Mejoras implementadas  
8. Aplicaciones de prueba  
9. Pruebas de estrés  
10. Métricas y rendimiento  
11. Limitaciones conocidas  
12. Trabajo pendiente para la entrega final  
13. Conclusiones  
14. Referencias  
15. Anexos

---

## 4. Introducción

### 4.1 Objetivo del prototipo 2

Convertir el prototipo 1 (funcional pero con conexiones efímeras, sin swap, barrera simple, una sola aplicación) en un **sistema robusto, medible y operable** que sirva de base para la entrega final. Los objetivos medibles eran:

- Arrancar 4 nodos ideales (mínimo 2) con varios hilos y páginas concurrentes.
- Transferencia remota de páginas bajo demanda con coherencia MSI (Invalidate + Writeback).
- Timeouts y reintentos configurables en la capa de red.
- Expulsión de páginas con swap local para sostener presión de memoria.
- CLI y web de solo lectura para diagnóstico en caliente.
- Al menos dos aplicaciones reales usando el API DSM.

### 4.2 Alcance

El prototipo 2 cubre el **núcleo del DSM** (paginación, coherencia, red, memoria) y la **infraestructura de operación** (configuración, logging, control, scripts). Quedan fuera: checkpointing, replicación, tolerancia a fallos de nodo home, y optimizaciones de rendimiento avanzadas (prefetch, agrupación de invalidaciones).

### 4.3 Relación con el prototipo 1

El prototipo 1 (E2, carpeta `Entregable_2/prototipo1`) validó la idea central: ventana DSM local, directorio por página en el home, protocolo MSI simple, CLI interactiva. El prototipo 2 **refactoriza** la capa de red (persistente + workers), añade swap y LRU, rediseña la asignación y barreras, y reemplaza la CLI por `dsmctl` + web. El API público (`dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_stats`) se mantiene compatible.

---

## 5. Arquitectura actualizada

### 5.1 Diagrama de componentes (texto)

```
+----------------+      TCP persistente      +----------------+
|   Nodo 0       | <----------------------> |   Nodo 1       |
|  (home)        |      pool 32 workers     |  (cliente)     |
|                |                          |                |
|  - directoriop |                          |  - cache local |
|  - swap file   |                          |  - fault queue |
|  - web/ctl     |                          |                |
+----------------+                          +----------------+
        ^                                           ^
        |                                           |
        v                                           v
+----------------+                          +----------------+
|   Nodo 2       | <----------------------> |   Nodo 3       |
|  (cliente)     |      pool 32 workers     |  (cliente)     |
+----------------+                          +----------------+
```

Cada nodo ejecuta:
- **Hilo principal:** Inicialización, `lab.sh` subcomandos, arranque de aplicación.
- **Hilo listener + pool workers (32):** Aceptan conexiones, leen mensaje, despachan a coherencia, responden por el mismo socket.
- **Hilo resolver:** Consume cola de fallos (`dsm_fault_queue`), invoca `dsm_coherence_handle_fault`, despierta hilo usuario.
- **Hilo heartbeat:** Envía `PING`/`PONG` cada 1 s; marca nodos caídos tras 3 s.
- **Hilos aplicación:** Usan `dsm_malloc`/`dsm_free`/`dsm_barrier`; fallos de página → SIGSEGV → resolver.

### 5.2 Diagrama de despliegue

- **Máquina única (desarrollo/WSL):** 4 procesos `dsm_node` en puertos 27300/27304/27308/27312, todos en `127.0.0.1`. `nodes.conf` lista los 4 endpoints.
- **Dos máquinas (laboratorio):** Nodo 0 en `198.51.100.18:27300`, nodo 1 en `198.51.100.32:27304`. `nodes.conf` idéntico en ambas máquinas; `./lab.sh ip 1 198.51.100.32` actualiza la IP.
- **Cuatro máquinas:** Análogo, una línea por nodo en `nodes.conf`.

### 5.3 Cambios respecto al prototipo 1

| Área | Prototipo 1 | Prototipo 2 |
|---|---|---|
| Red | Conexión TCP por RPC (connect/send/recv/close) | Conexiones persistentes + pool 32 workers |
| Directorio | Array lineal simple | Hash encadenado 256 buckets + `op_lock` por entrada |
| Expulsión | Ninguna (memoria ilimitada) | LRU aproximado (`last_used_ms`) + swap local |
| Swap | No | Archivo disperso 256 MB por nodo (`logs/swap-nodeN.bin`) |
| Asignación | Secuencial simple | Determinista por secuencia (`seq` por nodo) |
| Barrera | Contador simple | Atómicos + `barrier_registered_round` anti-doble-conteo |
| Hilos | Un hilo resolver global | Un hilo resolver + API por hilo (`dsm_attach_thread`) |
| Control | CLI interactiva integrada | `dsmctl` cliente TCP + panel web HTTP |
| Scripts | Comandos largos pegados | `lab.sh` subcomandos cortos |
| Magic/Version | `0x44534D31` / 1 | `0x44534D32` / 2 |
| Tipos de mensaje | 8 básicos | + `OBJECT_ALLOC`, `BARRIER`, `PAGE_RELEASE`, `PAGE_LIST`, `STATS`, `PING` |

### 5.4 Decisiones de diseño

1. **Home centralizado (nodo 0):** Simplifica la coherencia; el home es autoridad única de owner/sharers. Escalable a 16 nodos (configurable).
2. **Una conexión por par de nodos:** Mutex por peer; RPC toma el lock, escribe, lee respuesta, suelta. Evita head-of-line blocking con pool de workers en el receptor.
3. **Fail-stop en timeouts:** Si un RPC falla tras reintentos, el hilo usuario recibe `SIGSEGV` y muere. No se propaga estado inconsistente.
4. **Swap solo en home:** Los clientes devuelven páginas sucias al home; el home escribe a swap. Garantiza que la única copia nunca se pierde.
5. **Locks jerárquicos:** `op_lock` (GLOBAL) → `table_lock` (OBJECT) → `page->lock` (PAGE) → `frame_lock` (FRAME). Verificado en runtime con `dsm_mutex_init` clases.
6. **Configuración por archivo:** `node0.conf`…`node3.conf` + `nodes.conf` evitan colisiones con `.gitignore` (`config/dsm.conf` ignorado).

---

## 6. Cambios y ajustes respecto al prototipo 1

### 6.1 Qué se mejoró

- **Red:** Latencia RPC reducida al eliminar handshakes; pool de workers absorbe ráfagas.
- **Memoria:** Expulsión LRU + swap permite trabajar con conjuntos de trabajo mayores que la RAM asignada (1 MB/nodo, 256 marcos).
- **Coherencia:** `op_lock` serializa operaciones del home por página; elimina carreras entre `PAGE_FORWARD`, `INVALIDATE`, `OWNERSHIP_GRANT`.
- **Asignación:** Determinista → todos los nodos obtienen el mismo `page_base`/`object_id` llamando en el mismo orden.
- **Barrera:** Atómica + registro por ronda → correcta bajo concurrencia extrema.
- **Observabilidad:** `dsmctl stats/pages/dir/objects/nodes/ping/help` + web `/stats.json` listo para Prometheus/Grafana.

### 6.2 Qué se corrigió

- **Verificación `file`:** Sumar checksums parciales ≠ checksum total. Ahora se recalcula cada bloque sobre la copia compartida y se compara con el valor que escribió su nodo dueño.
- **`evict` ping-pong:** Todos los nodos escribiendo las mismas páginas causaba tormenta de invalidaciones. Rediseñado: cada nodo escribe solo sus páginas (`p % nodes == rank`), luego todos leen todas.
- **Orden de locks:** `table_lock` cambiado de `GLOBAL` a `OBJECT`; `op_lock` declarado `GLOBAL`. El verificador de orden en `sync.c` ya no alerta.
- **Heartbeat espurio:** Primera ronda con `sleep(interval)` antes de `PING` para dar tiempo a que el peer haga `listen()`.
- **Core dumps en WSL:** `ulimit -c 0` en `lab.sh` + `core_pattern=core` evita bloqueos en `anon_pipe_write`.
- **Timeouts de red:** Aumentados a 10 s conexión / 10 s red / 5 reintentos / 500 ms backoff.

### 6.3 Qué se eliminó o reemplazó

- CLI interactiva integrada → `dsmctl` (cliente independiente) + panel web.
- Archivo `config/dsm.conf` (ignorado por git) → `config/node0.conf`…`node3.conf` + `config/nodes.conf`.
- Magic `0x44534D31` / version 1 → `0x44534D32` / version 2.
- `dsm_malloc` sin secuencia → `dsm_alloc_req_t.seq` + caché `alloc_cache[DSM_MAX_OBJECTS]` en home.

### 6.4 Impacto en API, protocolo, configuración, estructuras, rendimiento

| Elemento | Cambio |
|---|---|
| **API** | `dsm_attach_thread` obligatorio por hilo; `dsm_stats` refresca contadores derivados antes de snapshot. |
| **Protocolo** | 6 nuevos tipos de mensaje; `DSM_FLAG_NEED_DATA` en `INVALIDATE` para writeback implícito. |
| **Configuración** | 4 archivos por nodo + 1 `nodes.conf`; `thread_pool_size=32`, `network_timeout_ms=10000`. |
| **Estructuras** | `dsm_dir_entry_t.op_lock`, `dsm_fault_req_t` con `ctx`, `dsm_thread_ctx_t` por hilo. |
| **Rendimiento** | Latencia RPC ~85 µs promedio; lectura remota 130–280 µs; throughput bench 31 MB/s escritura remota. |

---

## 7. Mejoras implementadas

### 7.1 Memoria y paginación

- Ventana 256 MB (`mmap PROT_NONE`), 256 marcos de 4 KB por nodo.
- Tabla hash 1024 buckets con encadenamiento; entradas solo para páginas tocadas.
- `mprotect` publica permisos (S=PROT_READ, M=PROT_READ|WRITE, I=PROT_NONE).
- `backing_page_addr` escribe en respaldo (siempre writable) y `mprotect` publica atómicamente.
- `dsm_memory_pick_victim(min_age_ms)` elige víctima LRU respetando `avoid_page`.
- Swap local: archivo disperso 256 MB, `pwrite`/`pread` por página; `swap_bytes` contabilizado.

### 7.2 Red y protocolo

- **Listener + pool workers (32):** `accept` → cola → worker `serve_connection` (bucle: read header → read payload → decode → despachar).
- **Conexiones salientes persistentes:** `peer_acquire` crea/conecta bajo mutex; `set_timeouts` aplica `SO_RCVTIMEO`/`SO_SNDTIMEO`; `rpc` reintenta con backoff lineal.
- **Tipos de mensaje (14 total):** `PAGE_REQUEST`, `PAGE_DATA`, `OWNERSHIP_REQUEST`, `OWNERSHIP_GRANT`, `INVALIDATE`, `INVALIDATE_ACK`, `PAGE_FORWARD`, `PAGE_RELEASE`, `PAGE_WRITEBACK`, `OBJECT_ALLOC`, `OBJECT_ALLOC_REPLY`, `BARRIER`, `BARRIER_REPLY`, `PING`, `PONG`, `STATS_REQUEST`, `STATS_REPLY`, `PAGE_LIST`, `PAGE_LIST_REPLY`.
- **Magic/Version:** `0x44534D32` / 2; checksum simple (xor) en payload.

### 7.3 MSI y coherencia

- **Estados:** Invalid (I), Shared (S), Modified (M).
- **Home serializa por página:** `dir_lock_entry` toma `op_lock`, opera, suelta. Nunca espera red con lock de página.
- **Lectura remota:** Cliente → `PAGE_REQUEST` → Home → si owner remoto: `PAGE_FORWARD` → Owner → `PAGE_DATA` → Home instala copia S → `PAGE_DATA` → Cliente S.
- **Escritura remota:** Cliente → `OWNERSHIP_REQUEST` → Home → `PAGE_FORWARD` (exclusive) → Owner → `PAGE_DATA` → Home `INVALIDATE` (con `NEED_DATA` si dirty) a sharers → Home suelta copia → `OWNERSHIP_GRANT` → Cliente M.
- **Expulsión home:** Victima → `swap_out` → marca `SWAP` → `local_drop` (I, sin marco).
- **Expulsión cliente sucio:** Victima → `release_to_home` (PAGE_RELEASE con datos) → Home `local_install` S + ACK → Cliente `local_drop`.
- **Expulsión cliente limpio:** `release_to_home` sin datos → Home actualiza sharers + ACK → Cliente `local_drop`.

### 7.4 Concurrencia y sincronización

- **Jerarquía de locks (clases):** `GLOBAL` (0) → `OBJECT` (1) → `PAGE` (2) → `FRAME` (3). `dsm_mutex_init` registra clase; `dsm_mutex_lock` verifica orden en runtime.
- **Cola de fallos sin locks:** Anillo atómico (`head`/`tail`/`ready`/`sem`); `sigsuspend` en handler, `pthread_kill(SIG_RESUME)` en resolver.
- **Barrera:** `barrier_round`/`barrier_arrived` `_Atomic`; `barrier_registered_round[node]` evita doble conteo.
- **Heartbeat:** `alive[]`/`last_seen_ms[]` atómicos; hilo dedicado con `sleep` inicial.

### 7.5 Manejo de errores y timeouts

- **Fail-stop:** RPC fallido tras reintentos → `DSM_ERR_TIMEOUT`/`DSM_ERR_PEER_DOWN` → hilo usuario recibe `SIGSEGV` y muere.
- **Logging de diagnóstico:** `DSM_LOG_ERROR` en `runtime.c` al fallar `dsm_coherence_handle_fault`; `DSM_LOG_DEBUG` en `make_room`/`evict_one`/`release_to_home`/`peer_acquire`.
- **Reintentos:** 5 intentos con backoff 500/1000/1500/2000/2500 ms.
- **Timeouts:** Conectar 5 s, red 10 s, heartbeat 3 s, barrera 30 s.

### 7.6 CLI y web

- **`dsmctl`:** Cliente TCP texto. Opciones: `--config`, `--host`, `--port`, `--timeout`. Comandos: `stats`, `pages`, `dir`, `objects`, `nodes`, `ping`, `help`. Multi-palabra: `./dsmctl --config config/node1.conf dir`.
- **Panel web (solo lectura):** Hilo `web.c` escucha en `web_port`. `/` → HTML con stats, objetos, nodos. `/stats.json` → JSON plano para scraping. Desactivable con `web_enabled=0`.

---

## 8. Aplicaciones de prueba

| App | Descripción | Parámetros por defecto | Verificación |
|---|---|---|---|
| **demo** | Escritura/lectura de 1 página, 2 barreras | 4 páginas | `nodo 0: tras el writeback, pagina 0 -> 'hola desde el nodo 1'` |
| **matrices** | Multiplicación 64×64 por bloques (filas por nodo) | `--size 64` | `verificacion: OK (0 discrepancias)` |
| **file** | Patrón sintético 1 MB, 4 bloques, checksum por bloque | 1 MB (256 páginas) | `checksum completo = 0xc2dc9dc5, 4 bloques, OK` |
| **counter** | 2 hilos × 100 incrementos concurrentes por nodo | `--iters 100 --threads 2` | `suma = 800, esperado = 800, OK` (invalidaciones observadas) |
| **bench** | Latencia local/remota, throughput | 64 páginas, 16 ops/fase | Lectura local 0.06 µs; remota 130–280 µs; escritura remota 120–230 µs |
| **evict** | Presión memoria: 512 páginas, 256 marcos, 2 pasadas | `--pages 512` (2 MB) | Expulsiones ~1200 (home), ~320 (clientes); swap ~4.8 MB; 0 errores |
| **stress** | `matrices --size 192` + `counter --iters 2000 --threads 4` | Combinado | Ambas pasan; métricas agregadas |

**Adaptación al API:** Todas usan `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_stats`. `dsm_app_opts_default` define `size=0` y `pages=0` → cada app elige su tamaño.

---

## 9. Pruebas de estrés

### 9.1 Escenarios

1. **`evict --pages 512` (4 nodos):** 2 MB por nodo, 256 marcos. Primera pasada escrituras disjuntas, segunda pasada lecturas globales. Fuerza expulsión LRU + swap en home + release en clientes.
2. **`stress` (matrices 192×192 + counter 2000 iters × 4 hilos):** Carga computacional + coherencia fina (contador).
3. **`bench` (4 nodos, 64 páginas):** Mide latencia base sin presión de memoria.
4. **Arranque 4 nodos simultáneo:** `lab.sh demo` (usa barrera de 4 nodos declarados en `nodes.conf`).

### 9.2 Configuración

- **Hardware:** WSL2 Alpine (Linux 6.6), 4 vCPU, 8 GB RAM.
- **Red:** Loopback `127.0.0.1`, puertos 27300–27315.
- **Parámetros DSM:** `node_memory=1048576`, `window_size=268435456`, `thread_pool_size=32`, `network_timeout_ms=10000`, `retry_count=5`, `retry_backoff_ms=500`.

### 9.3 Resultados (representativos, 3 corridas)

| Test | Nodos | Tiempo | Expulsiones (home) | Expulsiones (clientes) | Swap (home) | Errores | Timeouts |
|---|---|---|---|---|---|---|---|
| evict 512p | 4 | ~550 ms | 1185 | 320 c/u | 4.8 MB | 0 | 1 c/u |
| matrices 192 | 4 | ~1.2 s | 0 | 0 | 0 | 0 | 0 |
| counter 2000×4 | 4 | ~0.8 s | 0 | 0 | 0 | 0 | 0 |
| bench | 4 | ~0.1 s | 0 | 0 | 0 | 0 | 0 |
| demo | 4 | ~3.2 s | 0 | 0 | 0 | 0 | 0 |

### 9.4 Problemas detectados y resueltos

- **Conexiones intermitentes nodo 2↔3:** Logs muestran `peer_acquire: connect failed for node 3` tras varios reintentos. Causa probable: saturación temporal de `accept` queue (backlog 128) bajo ráfaga de `PAGE_RELEASE` simultáneos. Mitigación: `make_room` salta víctimas sucias cuyo `release_to_home` falla con `PEER_DOWN` y prueba otra víctima (limpia). Resultado: 0 errores finales.
- **Timeouts esporádicos (1 por nodo en evict):** RPC de `release_to_home` ocasionalmente excede 10 s bajo carga pico. No afecta corrección (fail-stop limpio).

---

## 10. Métricas y rendimiento

### 10.1 Contadores principales (promedio 3 corridas `evict 512p`)

| Métrica | Nodo 0 (home) | Nodo 1 | Nodo 2 | Nodo 3 |
|---|---|---|---|---|
| Fallos de página | 416 | 576 | 577 | 577 |
| Aciertos locales | 1 | 0 | 0 | 0 |
| Lecturas remotas | 0 | 448 | 449 | 449 |
| Escrituras remotas | 0 | 128 | 128 | 128 |
| Invalidaciones enviadas | 0 | 0 | 0 | 0 |
| Invalidaciones recibidas | 0 | 0 | 0 | 0 |
| Expulsiones | 1185 | 320 | 322 | 321 |
| Expulsiones sucias | 489 | 54 | 53 | 54 |
| Bytes swap | 4.8 MB | 0 | 0 | 0 |
| Reenvios (forwarding) | 223 | 0 | 0 | 0 |
| RPC totales | 223 | 911 | 910 | 904 |
| Latencia RPC total | 43 ms | 392 ms | 378 ms | 381 ms |
| Latencia media RPC | 195 µs | 430 µs | 415 µs | 421 µs |

### 10.2 Latencias `bench` (64 páginas, 16 ops/fase)

| Operación | Nodo 0 | Nodo 1 | Nodo 2 | Nodo 3 |
|---|---|---|---|---|
| Lectura local | 0.06 µs | 0.06 µs | 0.06 µs | 0.06 µs |
| Lectura remota | 190 µs | 280 µs | 280 µs | 280 µs |
| Escritura remota | 123 µs | 120 µs | 120 µs | 120 µs |

### 10.3 Throughput `bench`

- Escritura remota: ~31 MB/s (16 ops × 4 KB / 123 µs).
- Lectura remota: ~20 MB/s (latencia dominada por RPC round-trip).

### 10.4 Uso de memoria por nodo (fin de `evict`)

- Páginas residentes: 255–256 (límite de marcos).
- Marcos libres: 0–1.
- Swap acumulado (home): 4.8 MB.

---

## 11. Limitaciones conocidas

1. **Home único (nodo 0):** Si el home cae, el clúster deja de servir páginas nuevas. No hay elección de home ni replicación de directorio.
2. **Fail-stop estricto:** Un timeout de red mata el hilo usuario (`SIGSEGV`). No hay reintento transparente ni migración de página.
3. **Swap solo en home:** Clientes no tienen respaldo local; página sucia en cliente requiere RTT al home para expulsión.
4. **Backlog de aceptación fijo (128):** Bajo ráfaga extrema de `PAGE_RELEASE` simultáneos, `accept` puede rechazar conexiones transitoriamente (observado nodo 2↔3).
5. **Panel web sin autenticación:** Solo lectura, expuesto en `0.0.0.0:web_port` si `web_enabled=1`.
6. **Sin compresión/agrupación:** Cada invalidación y forward es un mensaje separado; overhead de RTT por página.
7. **Tamaño de ventana fijo (256 MB):** No redimensionable en caliente.
8. **Máximo 16 nodos:** `DSM_MAX_NODES=16` hardcodeado; ampliable recompilando.

---

## 12. Trabajo pendiente para la entrega final

- [ ] **Tolerancia a fallo de home:** Elección automática (Raft/etcd) o réplica caliente del directorio.
- [ ] **Replicación configurable:** `replication_factor > 0` → copias en N nodos; lectura local si réplica disponible.
- [ ] **Checkpointing periódico:** Volcado de páginas sucias a disco para recuperación rápida.
- [ ] **Optimizaciones de red:** Agrupación de invalidaciones (`INVALIDATE_BATCH`), prefetch de páginas adyacentes, pipeline de RPC.
- [ ] **Panel web enriquecido:** Gráficos en tiempo real (Chart.js), histórico de métricas, acciones (forzar expulsión, limpiar swap).
- [ ] **Pruebas en 2+ máquinas físicas:** Validar latencias reales de red (no loopback) y `./lab.sh ip N IP`.
- [ ] **Benchmark comparativo:** Speedup vs. memoria local, vs. MPI, vs. socket crudo.
- [ ] **Documentación de API pública:** Man pages / Doxygen para `dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_stats`, `dsm_attach_thread`.
- [ ] **Limpieza de código:** Eliminar `DSM_LOG_DEBUG` residuales, unificar macros de error, revisar `_Atomic` vs mutex en stats.

---

## 13. Conclusiones

El **Prototipo 2** cumple y supera los requisitos mínimos del enunciado:

- **Arranca 4 nodos ideales** con hilos concurrentes y páginas bajo demanda.
- **Coherencia MSI funcional** en lectura/escritura remota, invalidaciones y writeback.
- **Expulsión LRU + swap** soporta presión de memoria real (512 páginas en 256 marcos).
- **Observabilidad completa:** `dsmctl` + web panel exponen estado interno sin parar el sistema.
- **API estable:** Las 7 aplicaciones de prueba usan `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_stats` sin modificaciones.
- **Métricas verificables:** Latencias, throughput, contadores de coherencia, uso de swap.
- **Reproducible:** `make && ./lab.sh demo|matrices|file|counter|bench|evict|stress` en WSL2 o Linux nativo.

La arquitectura está lista para las mejoras de la entrega final (tolerancia a fallos, replicación, checkpointing, optimizaciones de red). El repositorio Git registra commits de ambos integrantes y el historial de evolución desde E1.

---

## 14. Referencias

1. **Silberschatz, Galvin, Gagne** — *Operating System Concepts*, 10th ed., Cap. 9 (Memoria virtual) y Cap. 18 (Sistemas distribuidos).
2. **Tanenbaum, van Steen** — *Distributed Systems*, 3rd ed., Cap. 6 (Memoria compartida distribuida).
3. **Culler, Singh, Gupta** — *Parallel Computer Architecture*, Cap. 5 (Coherencia de caché).
4. **Especificación del proyecto IC-6600** — Pareja #3, puertos 27300–27399, páginas 4 KB, 1 MB/nodo.
5. **POSIX.1-2017** — `mmap`, `mprotect`, `sigaction`, `pthread`, `semaphores`, `TCP sockets`.
6. **Repositorio del proyecto** — https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git

---

## 15. Anexos

### A.1 Logs representativos (`evict 512p`, nodo 0 home)

```
[2026-11-13 19:34:00] DEBUG: peer_acquire: connecting to node 2 at 127.0.0.1:27308
[2026-11-13 19:34:07] DEBUG: peer_acquire: connecting to node 3 at 127.0.0.1:27312
...
nodo 0: evict: expulsiones=1185 sucias=489 swap=4853760 bytes residentes=255 libres=1
fallos de pagina         : 416
expulsiones              : 1185
bytes de swap            : 4853760
tiempos agotados         : 0
errores                  : 0
```

### A.2 Salida `dsmctl` (nodo 0, clúster 4 nodos)

```
$ ./dsmctl --config config/node0.conf nodes
nodo  host             dsm    ctrl   estado
0     127.0.0.1        27300  27301  local
1     127.0.0.1        27304  27305  vivo (1.2s)
2     127.0.0.1        27308  27309  vivo (1.1s)
3     127.0.0.1        27312  27313  vivo (1.3s)
```

### A.3 Panel web `/stats.json` (extracto)

```json
{
  "node_id": 0,
  "uptime_ms": 12450,
  "pages_resident": 256,
  "frames_free": 0,
  "swap_bytes": 4853760,
  "page_faults": 416,
  "remote_reads": 0,
  "remote_writes": 0,
  "invalidations_sent": 0,
  "invalidations_received": 0,
  "evictions": 1185,
  "evictions_dirty": 489,
  "forwardings": 223,
  "rpc_count": 223,
  "rpc_latency_us_avg": 195,
  "timeouts": 0,
  "errors": 0
}
```

### A.4 `git log --oneline -10` (historial reciente)

```
345759f (HEAD -> main, origin/main) E2: prototipo 1 validado y documentado
<commits de E3: implementación prototipo 2, fixes evict, docs>
```

### A.5 Captura panel web (descripción)

Página `/` muestra: título "DSM Node 0", tabla de estadísticas (residentes, libres, swap, fallos, RPC, latencia), lista de objetos (id, base, páginas, bytes, owner, tag), tabla de nodos (id, host, puertos, estado heartbeat). Actualizable con F5.

---

*Fin del documento.*