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
- **Expulsión LRU + swap:** El nodo home mantiene el archivo de intercambio (`logs/swap-nodeN.bin`); los clientes devuelven sus páginas sucias al home, así que la única copia nunca se pierde.
- **Asignación determinista:** `dsm_malloc` usa un número de secuencia por nodo; todos los nodos que llaman en el mismo orden reciben el mismo rango global de páginas.
- **Barrera centralizada:** Contadores atómicos y registro por nodo evitan doble conteo.
- **API por hilo:** Cada hilo se adjunta a la ventana DSM con `dsm_attach` (y se desregistra con `dsm_detach`); los fallos de página se resuelven en un hilo resolver dedicado.
- **Observabilidad:** `dsmctl` (texto sobre TCP) y panel web (`/`, `/stats.json`) exponen estadísticas, páginas, directorio, objetos y nodos.
- **Scripts de operación:** `lab.sh` unifica build, demo, apps, control, IP, show, stop en comandos cortos.

**Validación:** en WSL2 (Alpine) con 4 procesos locales sobre `127.0.0.1`. `matrices` (64×64 y 192×192) corre con 4 nodos; `demo`, `file`, `counter`, `bench` y `evict` corren con la tabla reducida `nodes_2.conf` (2 nodos), que es la configuración estable. Todos terminan con 0 errores y 0 timeouts. Métricas de latencia: lectura local ~0.06 µs, lectura remota ~130–280 µs, escritura remota ~120–230 µs. Las limitaciones que quedan abiertas se detallan en §11.

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
|  - directorio  |                          |  - cache local |
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
- **Hilo principal:** Inicialización, parseo de argumentos (`--config`, `--nodes`, `--app`) y arranque de la aplicación.
- **Hilo listener + pool workers (32):** Aceptan conexiones, leen mensaje, despachan a coherencia, responden por el mismo socket.
- **Hilo resolver:** Consume cola de fallos (`dsm_fault_queue`), invoca `dsm_coherence_handle_fault`, despierta hilo usuario.
- **Hilo heartbeat:** Envía `PING`/`PONG` cada 1 s; marca nodos caídos tras 3 s.
- **Hilos aplicación:** Usan `dsm_malloc`/`dsm_free`/`dsm_barrier`; fallos de página → SIGSEGV → resolver.

### 5.2 Diagrama de despliegue

- **Máquina única (desarrollo/WSL):** 4 procesos `dsm_node` en los puertos 27300/27304/27308/27312, todos en `127.0.0.1`. `nodes.conf` declara los 4 endpoints y lo usan `matrices` y `serve`; `nodes_2.conf` declara solo los nodos 0 y 1 y lo usan `demo`, `file`, `counter`, `bench` y `evict`. La barrera cuenta los nodos del archivo que se pasa con `--nodes`.
- **Dos máquinas (objetivo de la demo final, pendiente de validar):** nodo 0 en `<IP-A>:27300`, nodo 1 en `<IP-B>:27304`. `nodes.conf` y `nodes_2.conf` deben ser idénticos en ambas máquinas; `./lab.sh ip 1 <IP-B>` actualiza la dirección del nodo 1 en las dos tablas sin editar los archivos a mano.
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
| Hilos | Un hilo resolver global | Un hilo resolver + `dsm_attach`/`dsm_detach` por hilo |
| Control | CLI interactiva integrada | `dsmctl` cliente TCP + panel web HTTP |
| Scripts | Comandos largos pegados | `lab.sh` subcomandos cortos |
| Magic/Version | `0x44534D31` / 1 | `0x44534D32` / 2 |
| Tipos de mensaje | 12 básicos | 23 en total (asignación, barrera, release, listado, stats, ping) |

### 5.4 Decisiones de diseño

1. **Home centralizado (nodo 0):** Simplifica la coherencia; el home es autoridad única de owner/sharers. Escalable a 16 nodos (configurable).
2. **Una conexión por par de nodos:** Mutex por peer; RPC toma el lock, escribe, lee respuesta, suelta. Evita head-of-line blocking con pool de workers en el receptor.
3. **Fail-stop en timeouts:** Si un RPC falla tras reintentos, el hilo usuario recibe `SIGSEGV` y muere. No se propaga estado inconsistente.
4. **Swap solo en home:** Los clientes devuelven páginas sucias al home; el home escribe a swap. Garantiza que la única copia nunca se pierde.
5. **Locks jerárquicos:** `op_lock` (GLOBAL) → `table_lock` (OBJECT) → `page->lock` (PAGE) → `frame_lock` (FRAME). Verificado en runtime con `dsm_mutex_init` clases.
6. **Configuración por archivo:** `node0.conf`…`node3.conf` + `nodes.conf` (4 nodos) y `nodes_2.conf` (2 nodos); se evita así la colisión con `.gitignore` (`config/dsm.conf` está ignorado).

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
- **Timeouts de red:** Aumentados a 5 s de conexión, 10 s de red, 5 reintentos con backoff lineal de 500 ms y pool de 32 workers.

### 6.3 Qué se eliminó o reemplazó

- CLI interactiva integrada → `dsmctl` (cliente independiente) + panel web.
- Archivo `config/dsm.conf` (ignorado por git) → `config/node0.conf`…`node3.conf` + `config/nodes.conf`.
- Magic `0x44534D31` / version 1 → `0x44534D32` / version 2.
- `dsm_malloc` sin secuencia → `dsm_alloc_req_t.seq` + caché `alloc_cache[DSM_MAX_OBJECTS]` en home.

### 6.4 Impacto en API, protocolo, configuración, estructuras, rendimiento

| Elemento | Cambio |
|---|---|
| **API** | `dsm_attach`/`dsm_detach` obligatorios por hilo; `dsm_stats` refresca contadores derivados antes del snapshot. |
| **Protocolo** | 11 tipos de mensaje nuevos; `DSM_FLAG_NEED_DATA` en `INVALIDATE` para writeback implícito. |
| **Configuración** | 4 archivos por nodo (`node0.conf`…`node3.conf`) + `nodes.conf` + `nodes_2.conf`; `thread_pool_size=32`, `network_timeout_ms=10000`. |
| **Estructuras** | `dsm_dir_entry_t.op_lock`, `dsm_fault_req_t` con `ctx`, `dsm_thread_ctx_t` por hilo. |
| **Rendimiento** | Latencia media RPC 130–430 µs según carga; lectura remota 130–280 µs; escritura remota 120–230 µs. |

---

## 7. Mejoras implementadas

### 7.1 Memoria y paginación

- Ventana 256 MB (`mmap PROT_NONE`), 256 marcos de 4 KB por nodo.
- Tabla hash 1024 buckets con encadenamiento; entradas solo para páginas tocadas.
- `mprotect` publica permisos (S=PROT_READ, M=PROT_READ|WRITE, I=PROT_NONE).
- `backing_page_addr` escribe en respaldo (siempre writable) y `mprotect` publica atómicamente.
- `dsm_memory_pick_victim(min_age_ms, avoid_page)` elige la víctima LRU y nunca devuelve la página que se está instalando (`avoid_page`).
- Swap local: archivo disperso 256 MB, `pwrite`/`pread` por página; `swap_bytes` contabilizado.

### 7.2 Red y protocolo

- **Listener + pool workers (32):** `accept` → cola → worker `serve_connection` (bucle: read header → read payload → decode → despachar).
- **Conexiones salientes persistentes:** `peer_acquire` crea/conecta bajo mutex; `set_timeouts` aplica `SO_RCVTIMEO`/`SO_SNDTIMEO`; `rpc` reintenta con backoff lineal.
- **Tipos de mensaje (23 en total):** asignación `OBJECT_ALLOC`, `OBJECT_ALLOC_REPLY`; barrera `BARRIER`, `BARRIER_REPLY`; páginas `PAGE_REQUEST`, `PAGE_DATA`, `PAGE_FORWARD`, `PAGE_WRITEBACK`, `PAGE_WRITEBACK_ACK`, `PAGE_RELEASE`, `PAGE_LIST`, `PAGE_LIST_REPLY`; propiedad `OWNERSHIP_REQUEST`, `OWNERSHIP_GRANT`, `OWNERSHIP_DENY`; coherencia `INVALIDATE`, `INVALIDATE_ACK`; observabilidad `STATS_REQUEST`, `STATS_REPLY`, `PING`, `PONG`; y los genéricos `ERROR`, `ACK`.
- **Magic/Version:** `0x44534D32` / 2; checksum FNV-1a de 32 bits sobre el payload, verificado al recibir.

### 7.3 MSI y coherencia

- **Estados:** Invalid (I), Shared (S), Modified (M).
- **Home serializa por página:** `dir_lock_entry` toma `op_lock`, opera, suelta. Nunca espera red con lock de página.
- **Lectura remota:** Cliente → `PAGE_REQUEST` → Home → si owner remoto: `PAGE_FORWARD` → Owner → `PAGE_DATA` → Home instala copia S → `PAGE_DATA` → Cliente S.
- **Escritura remota:** Cliente → `OWNERSHIP_REQUEST` → Home → `PAGE_FORWARD` (exclusive) → Owner → `PAGE_DATA` → Home `INVALIDATE` (con `NEED_DATA` si dirty) a sharers → Home suelta copia → `OWNERSHIP_GRANT` → Cliente M.
- **Expulsión home:** Víctima → `swap_out` → marca `SWAP` → `local_drop` (I, sin marco).
- **Expulsión cliente sucio:** Víctima → `release_to_home` (PAGE_RELEASE con datos) → Home `local_install` S + ACK → Cliente `local_drop`.
- **Expulsión cliente limpio:** `release_to_home` sin datos → Home actualiza sharers + ACK → Cliente `local_drop`.

### 7.4 Concurrencia y sincronización

- **Jerarquía de locks (clases):** `GLOBAL` (0) → `OBJECT` (1) → `PAGE` (2) → `FRAME` (3). `dsm_mutex_init` registra clase; `dsm_mutex_lock` verifica orden en runtime.
- **Cola de fallos sin locks:** Anillo atómico (`head`/`tail`/`ready`/`sem`); `sigsuspend` en handler, `pthread_kill(SIG_RESUME)` en resolver.
- **Barrera:** `barrier_round`/`barrier_arrived` `_Atomic`; `barrier_registered_round[node]` evita doble conteo.
- **Heartbeat:** `alive[]`/`last_seen_ms[]` atómicos; hilo dedicado con `sleep` inicial.

### 7.5 Manejo de errores y timeouts

- **Fail-stop:** RPC fallido tras reintentos → `DSM_ERR_TIMEOUT`/`DSM_ERR_PEER_DOWN` → hilo usuario recibe `SIGSEGV` y muere.
- **Logging de diagnóstico:** `DSM_LOG_ERROR` en `runtime.c` al fallar `dsm_coherence_handle_fault`; trazas `DSM_LOG_DEBUG` (nivel 3) en `make_room`/`evict_one`/`release_to_home`/`peer_acquire`, silenciosas con `log_level=2`. `fault_diag()` imprime a `stderr` la razón exacta cuando el handler decide relanzar el `SIGSEGV`.
- **Reintentos:** 5 intentos con backoff 500/1000/1500/2000/2500 ms.
- **Timeouts:** Conectar 5 s, red 10 s, heartbeat 3 s, barrera 30 s.

### 7.6 CLI y web

- **`dsmctl`:** Cliente TCP texto. Opciones: `--config`, `--host`, `--port`, `--timeout`. Comandos: `stats`, `pages`, `dir`, `objects`, `nodes`, `ping`, `help`. Multi-palabra: `./dsmctl --config config/node1.conf dir`.
- **Panel web (solo lectura):** Hilo `web.c` escucha en `web_port`. `/` → HTML con stats, objetos, nodos. `/stats.json` → JSON plano para scraping. Desactivable con `web_enabled=0`.

---

## 8. Aplicaciones de prueba

| App | Descripción | Parámetros por defecto | Verificación |
|---|---|---|---|
| **demo** | 1 página compartida: nodo 0 escribe, nodo 1 lee y escribe, nodo 0 relee; 3 barreras | 1 página | `nodo 0: tras el writeback, pagina 0 -> 'hola desde el nodo 1'` |
| **matrices** | Multiplicación por bloques de filas (filas por nodo) | `--size 64` (`--size 192` en stress) | `verificacion: OK (0 discrepancias)` |
| **file** | Patrón sintético de 1 MB; un bloque por nodo | 1 MB (256 páginas) | `checksum completo = 0xc2dc9dc5, 2 bloques, OK` (2 nodos) |
| **counter** | Cada hilo incrementa su ranura y lee la del vecino (invalidaciones) | `--iters 100 --threads 2` | `suma = 400, esperado = 400, OK` (2 nodos) |
| **bench** | 3 fases: lectura local, lectura remota, escritura remota | 64 páginas; `iters = páginas/nodos` (32 con 2 nodos) | Lectura local 0.06 µs; remota 130–280 µs; escritura remota 120–230 µs |
| **evict** | Presión de memoria: 64 páginas, 32 marcos, 2 pasadas | `--pages 64 --frames 32` | `expulsiones=123 sucias=60 swap=503808 bytes residentes=32 libres=0 (suma 0)` |
| **stress** (script) | `matrices --size 192` + `counter --iters 100 --threads 2` | Combinado | Matrices OK; counter 400/400 |

**Adaptación al API:** Todas usan `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_stats`. `dsm_app_opts_default` define `size=0` y `pages=0` → cada app elige su tamaño.

---

## 9. Pruebas de estrés

### 9.1 Escenarios

1. **`matrices --size 192` (4 nodos):** carga de CPU y coherencia sobre 192×192; es la parte pesada de `./lab.sh stress`.
2. **`evict --pages 64 --frames 32` (2 nodos):** 64 páginas en 32 marcos; fuerza expulsión LRU, swap en el home y `PAGE_RELEASE` en el cliente.
3. **`counter --iters 100 --threads 2` (2 nodos):** invalidaciones por falsos compartidos entre dos páginas de contadores.
4. **Arranque simultáneo de 4 nodos:** `./lab.sh matrices` levanta los 4 procesos de `nodes.conf` y la barrera los sincroniza.

### 9.2 Configuración

- **Hardware:** WSL2 (Alpine) sobre Windows, 4 vCPU, 8 GB RAM.
- **Red:** Loopback `127.0.0.1`, puertos 27300–27315.
- **Parámetros DSM:** `node_memory=1048576`, `window_size=268435456`, `thread_pool_size=32`, `network_timeout_ms=10000`, `connect_timeout_ms=5000`, `retry_count=5`, `retry_backoff_ms=500`.

### 9.3 Resultados (una corrida representativa por aplicación)

| Test | Nodos | Fallos de página | Expulsiones | Swap (home) | Errores | Timeouts |
|---|---|---|---|---|---|---|
| demo | 2 | 2 / 2 | 0 | 0 | 0 | 0 |
| matrices 64 | 4 | 18 / 12 / 12 / 12 | 0 | 0 | 0 | 0 |
| matrices 192 | 4 | 162 / 108 / 108 / 108 | 0 | 0 | 0 | 0 |
| file 1 MB | 2 | 259 / 129 | 0 | 0 | 0 | 0 |
| counter 100×2 | 2 | 3 / 2 | 0 | 0 | 0 | 0 |
| bench 64 páginas | 2 | 96 / 96 | 0 | 0 | 0 | 0 |
| evict 64p/32f | 2 | 65 / 93 | 123 (home) / 61 | 503808 B | 0 | 0 |

### 9.4 Problemas detectados y resueltos

- **Conexiones intermitentes nodo 2↔3 (solo con 4 nodos y 512 páginas):** los logs muestran `peer_acquire: connect failed for node 3` tras varios reintentos. Causa probable: saturación temporal de la cola de `accept` (backlog 128) bajo una ráfaga de `PAGE_RELEASE` simultáneos. Mitigación: `coherence_make_room` salta la víctima cuyo `release_to_home` falla con `PEER_DOWN`/`TIMEOUT` y prueba otra, hasta 64 intentos. Con la configuración de 2 nodos y 64 páginas el escenario no se reproduce.
- **Timeouts esporádicos en `evict` de 4 nodos con 512 páginas:** algún `release_to_home` excede los 10 s en el pico. No rompe la coherencia (fail-stop limpio), pero hace que la corrida no sea reproducible al 100 %, y por eso `./lab.sh evict` usa la tabla de 2 nodos.

---

## 10. Métricas y rendimiento

### 10.1 Contadores de `evict --pages 64 --frames 32` (2 nodos, corrida representativa)

| Métrica | Nodo 0 (home) | Nodo 1 |
|---|---|---|
| Fallos de página | 65 | 93 |
| Expulsiones | 123 | 61 |
| Expulsiones sucias | 60 | 21 |
| Bytes de swap | 503808 | 0 |
| Páginas residentes | 32 | 32 |
| Marcos libres | 0 | 0 |
| Reenvíos (forwarding) | 0 | 0 |
| Errores / timeouts | 0 / 0 | 0 / 0 |

En una corrida anterior del mismo escenario el home reportó `expulsiones=91 sucias=56 swap=372736`; el orden de magnitud es estable y el número exacto depende del entrelazado de las dos pasadas.

### 10.2 Latencias `bench` (64 páginas, 32 operaciones por fase con 2 nodos)

| Operación | Rango observado |
|---|---|
| Lectura local | ~0.06 µs |
| Lectura remota | 130–280 µs |
| Escritura remota | 120–230 µs |

La lectura local es un acceso a memoria normal (la página ya está en `S`/`M`); la remota incluye el `SIGSEGV`, el RPC al home y la transferencia de 4 KB.

### 10.3 Throughput `bench` (derivado de la latencia, no medido como agregado)

- Escritura remota: 4 KB / 120–230 µs ≈ 18–34 MB/s por operación.
- Lectura remota: 4 KB / 130–280 µs ≈ 15–31 MB/s por operación.

### 10.4 Uso de memoria por nodo (fin de `evict 64p/32f`)

- Páginas residentes: 32 de 32 marcos (pool deliberadamente pequeño con `--frames 32`).
- Marcos libres: 0; toda página nueva obliga a expulsar.
- Swap acumulado en el home: 503808 B (~0.5 MB).
- Verificación de datos: `(suma 0)`, es decir, la suma de comprobación de todas las páginas releídas coincide con lo escrito.

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
9. **`evict` de 4 nodos con 512 páginas no es reproducible al 100 %:** aparecen timeouts de red en el pico de `PAGE_RELEASE`. La configuración estable documentada es 2 nodos con 64 páginas y 32 marcos; el caso grande queda como prueba de estrés opcional.
10. **`counter` pierde incrementos con contención alta:** con 2000 iteraciones × 4 hilos se observó `suma = 8000, esperado = 16000`. Los dos nodos escriben en la misma página de 4 KB y el home cede la propiedad copiando su propia copia y soltándola; las escrituras de sus hilos de usuario que caen en esa ventana no viajan al nuevo dueño. Es un fallo real de la transferencia de propiedad del home, no una consecuencia inevitable de la granularidad de página. Con `--iters 100 --threads 2` el entrelazado no lo dispara y el resultado es determinista. El arreglo (una página por nodo) está en §12.
11. **Panel web no verificado en el entorno de pruebas:** la imagen Alpine usada no incluye `curl` ni un navegador, así que el HTML y el JSON no se pudieron solicitar; el hilo arranca y el código compila sin advertencias.

---

## 12. Trabajo pendiente para la entrega final

- [ ] **Tolerancia a fallo de home:** Elección automática (Raft/etcd) o réplica caliente del directorio.
- [ ] **Replicación configurable:** `replication_factor > 0` → copias en N nodos; lectura local si réplica disponible.
- [ ] **Checkpointing periódico:** Volcado de páginas sucias a disco para recuperación rápida.
- [ ] **Optimizaciones de red:** Agrupación de invalidaciones (`INVALIDATE_BATCH`), prefetch de páginas adyacentes, pipeline de RPC.
- [ ] **Panel web enriquecido:** Gráficos en tiempo real (Chart.js), histórico de métricas, acciones (forzar expulsión, limpiar swap).
- [ ] **Corregir `counter` con alta contención:** asignar una página de 4 KB por nodo (ranuras en `page_base + rank * page_size`) para que cada nodo escriba solo su página; además, revisar la cesión de propiedad del home (copiar bajo protección de escritura antes de soltar la copia). Luego volver a `--iters 2000 --threads 4` y verificar el total exacto.
- [ ] **Estabilizar `evict` de 4 nodos con 512 páginas:** revisar el backlog de `accept`, añadir reconexión en segundo plano o limitar la ráfaga de `PAGE_RELEASE`.
- [ ] **Pruebas en 2+ máquinas físicas:** Validar latencias reales de red (no loopback) y `./lab.sh ip N IP`.
- [ ] **Benchmark comparativo:** Speedup vs. memoria local, vs. MPI, vs. socket crudo.
- [ ] **Documentación de API pública:** Man pages / Doxygen para `dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_stats`, `dsm_attach`, `dsm_detach`.
- [ ] **Limpieza de código:** Unificar macros de error y revisar `_Atomic` vs mutex en `stats`.

---

## 13. Conclusiones

El **Prototipo 2** cumple y supera los requisitos mínimos del enunciado:

- **Arranca 4 nodos ideales** con hilos concurrentes y páginas bajo demanda.
- **Coherencia MSI funcional** en lectura/escritura remota, invalidaciones y writeback.
- **Expulsión LRU + swap** soporta presión de memoria real (64 páginas en 32 marcos, con swap en el home).
- **Observabilidad:** `dsmctl` verificado contra un nodo en vivo (stats, pages, dir, objects, nodes); panel web implementado y pendiente de verificación en un entorno con cliente HTTP.
- **API estable:** las 6 aplicaciones de prueba (`demo`, `matrices`, `file`, `counter`, `bench`, `evict`) usan `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_stats` sin modificaciones.
- **Métricas verificables:** latencias, fallos de página, expulsiones y uso de swap.
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

### A.1 Salida representativa de `evict --pages 64 --frames 32` (2 nodos)

```
nodo 0: evict: tocando 64 paginas (marco de 4 KB, pool limitado)
nodo 1: evict: tocando 64 paginas (marco de 4 KB, pool limitado)
nodo 0: evict: primera pasada lista
nodo 1: evict: primera pasada lista
nodo 0: evict: segunda pasada lista
nodo 1: evict: segunda pasada lista
nodo 0: evict: expulsiones=123 sucias=60 swap=503808 bytes residentes=32 libres=0 (suma 0)
fallos de pagina         : 65
expulsiones              : 123
bytes de swap            : 503808
tiempos agotados         : 0
errores                  : 0
nodo 1: evict: expulsiones=61 sucias=21 swap=0 bytes residentes=32 libres=0 (suma 0)
fallos de pagina         : 93
tiempos agotados         : 0
errores                  : 0
```

### A.2 Salida de `dsmctl nodes` (nodo 0 en vivo)

Formato que imprime `control.c`; la columna `ctrl` es `dsm + 1` y el estado sale del heartbeat (`local` para el propio nodo):

```
$ ./dsmctl --config config/node0.conf nodes
nodo  host             dsm    ctrl   estado
0     127.0.0.1        27300  27301  local
1     127.0.0.1        27304  27305  vivo
2     127.0.0.1        27308  27309  vivo
3     127.0.0.1        27312  27313  vivo
```

Durante la prueba de `./lab.sh ip 1 192.0.2.20` la misma consulta mostró el nodo 1 como `sin respuesta`, que es el comportamiento esperado cuando el peer no está escuchando en la dirección configurada.

### A.3 Panel web `/stats.json` (esquema emitido por `web.c`)

```json
{
  "node": 0,
  "nodes": 2,
  "page_size": 4096,
  "resident": 32,
  "frames_free": 0,
  "stats": {
    "page_faults": 65,
    "evictions": 123,
    "evictions_dirty": 60,
    "swap_bytes": 503808,
    "timeouts": 0,
    "errors": 0
  },
  "objects": [
    {"id": 1, "page_base": 0, "pages": 64, "owner": 0, "tag": 0}
  ],
  "peers": [
    {"id": 0, "host": "127.0.0.1", "port": 27300},
    {"id": 1, "host": "127.0.0.1", "port": 27304}
  ]
}
```

El objeto `stats` incluye todos los campos de `dsm_stats_field_name()`; aquí se muestran solo algunos. Este esquema **no se pudo solicitar** en el entorno de pruebas (Alpine sin `curl`); se transcribe de `dsm_web_render_json()` en `web.c`.

### A.4 `git log --oneline` (historial reciente)

```
dead508 feat(entregable-3): prototipo 2 con conexiones persistentes y operación del cluster
345759f E2: validación en laboratorio + fix warnings GCC 13.3.0
a1bd33f feat(entregable-2): integra el prototipo 1 y registra la ejecucion de dos nodos
```

### A.5 Captura panel web (descripción)

Página `/` muestra: título "DSM Node 0", tabla de estadísticas (residentes, libres, swap, fallos, RPC, latencia), lista de objetos (id, base, páginas, bytes, owner, tag), tabla de nodos (id, host, puertos, estado heartbeat). Actualizable con F5.

---

*Fin del documento.*