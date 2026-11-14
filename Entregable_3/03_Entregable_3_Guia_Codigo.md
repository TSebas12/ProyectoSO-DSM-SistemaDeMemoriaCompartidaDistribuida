# Guía de Código — Prototipo 2 DSM

**Archivo:** `Entregable_3/03_Entregable_3_Guia_Codigo.md`  
**Proyecto:** IC-6600 DSM — Prototipo 2  
**Convención:** Comentarios en español, identificadores en inglés `snake_case`.

---

## Estructura de directorios

```
Entregable_3/prototipo2/
├── include/           # 18 cabeceras públicas (*.h)
├── src/               # 25 módulos (*.c)
├── config/            # node0.conf..node3.conf, nodes.conf, nodes_2.conf
├── logs/              # generado al ejecutar (ignorado por git): dsm-nodeN.log, swap-nodeN.bin, run-nodeN.out
├── Makefile
├── lab.sh             # Subcomandos: build, demo, matrices, file, counter, bench, evict, stress, serve, ctl, ip, show, stop
└── README.md          # (raíz del repo)
```

---

## Módulos (include/*.h → src/*.c)

### Núcleo DSM

| Archivo | Responsabilidad | Funciones clave |
|---|---|---|
| `dsm_types.h` | Tipos base, códigos de error, constantes | `dsm_status_t`, `DSM_MAX_NODES`, `DSM_MAGIC` |
| `dsm_config.h` | Carga/validación de `dsm_config_t` (704 B) | `dsm_config_load`, `dsm_config_defaults` |
| `dsm_nodes.h` | Tabla de peers (`dsm_peer_t`: id, host, port) | `dsm_nodes_load`, `dsm_nodes_get` |
| `dsm_memory.h` | Ventana mmap, tabla hash páginas, marcos, swap | `dsm_memory_init`, `dsm_memory_pick_victim`, `dsm_memory_swap_out/in` |
| `dsm_sync.h` | Mutex/cond con clases y verificación de orden | `dsm_mutex_init(klass)`, `dsm_cond_wait` |
| `dsm_log.h` | Logging por niveles (ERROR..TRACE) a archivo/stderr | `DSM_LOG(level, fmt...)` |
| `dsm_stats.h` | Contadores atómicos + snapshot | `dsm_stats_inc`, `dsm_stats_snapshot` |
| `dsm_fault.h` | Handler SIGSEGV, cola lock-free, contexto por hilo | `dsm_fault_install`, `dsm_fault_attach_thread` |
| `dsm_coherence.h` | Directorio MSI, ops de coherencia, barrera, objetos | `dsm_coherence_handle_fault`, `dsm_barrier` |
| `dsm_net.h` | Transporte TCP persistente + pool workers | `dsm_net_start`, `dsm_net_send`, `dsm_net_call` |
| `dsm_api.h` | API pública: malloc/free/barrier/stats/attach | `dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_stats` |
| `dsm_runtime.h` | Orquestador: inicializa subsistemas, hilos servicios | `dsm_runtime_init`, `dsm_runtime_start_services` |
| `dsm_control.h` | Servidor texto TCP para `dsmctl` | `dsm_control_start/stop/dispatch` |
| `dsm_web.h` | Panel HTTP de solo lectura (`/`, `/stats.json`) | `dsm_web_start/stop/render_html` |
| `dsm_heartbeat.h` | PING/PONG periódico, detección de nodos caídos | `dsm_heartbeat_start/stop` |
| `dsm_apps.h` | Dispatcher de aplicaciones de prueba | `dsm_app_run` |
| `dsm_app_common.h` | Utilidades apps: barreras, checksum, splits | `dsm_app_split_start/count`, `dsm_app_checksum` |

### Implementación (src/*.c)

| Archivo | Qué implementa |
|---|---|
| `config.c` | Parseo `key = value`, validación rangos, defaults |
| `nodes.c` | Carga `nodes.conf` (id host [port]), búsqueda O(1) |
| `memory.c` | `mmap` ventana, hash páginas, `alloc_frame`/`release_frame`, LRU `pick_victim`, `swap_out/in` |
| `sync.c` | `pthread_mutex`/`cond` wrappers + `check_order` (clases GLOBAL/OBJECT/PAGE/FRAME) |
| `log.c` | `fprintf` con timestamp y nivel; `fflush` cada línea |
| `stats.c` | `_Atomic uint64_t` array + `snapshot` copia consistente |
| `fault.c` | `SIGSEGV` → `SEGV_ACCERR` → cola anillo → `sigsuspend`; el resolver despierta con `pthread_kill(DSM_SIG_RESUME)` |
| `coherence.c` | **Núcleo MSI**: directorio hash + `op_lock` por página, `home_serve_read/write`, `forward_from_owner`, `invalidate_node`, `local_install` (reintentos), `coherence_make_room` (evict loop), `handle_*` mensajes, barrera atómica, asignación determinista `alloc_cache[seq]` |
| `net.c` | Listener + 32 workers; `peer_acquire` conexiones salientes persistentes + mutex; `rpc` con reintentos/backoff; `t_reply_fd` para respuesta en misma conexión |
| `dsm_api.c` | `dsm_malloc` (seq + RPC `OBJECT_ALLOC` a home), `dsm_free` (`PAGE_RELEASE`), `dsm_barrier` (RPC `BARRIER`), `dsm_stats` (refresca derivados) |
| `runtime.c` | Inicializa orden: config → nodes → memory → sync → stats → fault → coherence → net → control → web → heartbeat → resolver → app |
| `control.c` | Loop: `read line` → `dispatch` (stats/pages/dir/objects/nodes/ping/help) → `write response` |
| `web.c` | `serve_connection` HTTP/1.0 minimal; `/` HTML, `/stats.json` JSON |
| `heartbeat.c` | Sleep inicial 1 intervalo; loop: `PING` a peers vivos; `alive[]` atómico |
| `apps.c` | `dsm_app_run` despacha por nombre; `dsm_app_opts_default` (size=0, pages=0) |
| `app_common.c` | `dsm_app_split_start/count`, `dsm_app_checksum` (xor simple) |
| `app_matrices.c` | Multiplicación por bloques de filas; verifica `C = A×B` |
| `app_file.c` | Patrón sintético `(i*31+7) mod 256`; cada nodo escribe su tramo; nodo 0 recalcula el checksum de cada bloque y lo compara |
| `app_counter.c` | Hilos `pthread` incrementan ranuras de un objeto DSM (`nodes*threads*8` bytes); barrera final; verifica la suma |
| `app_bench.c` | 3 fases: lectura local, lectura remota, escritura remota; imprime µs/op y MB/s |
| `app_evict.c` | 2 pasadas: (1) cada nodo escribe `p % nodes == rank`; (2) todos leen todos; reporta expulsiones/swap |
| `demo_page.c` | Demo mínima: nodo 0 escribe, barrera, nodo 1 lee y escribe, barrera, nodo 0 relee, barrera (los nodos ≥ 2 solo pasan las barreras) |
| `node_main.c` | `main`: parse args, `setvbuf(stdout,_IOLBF,0)`, `dsm_runtime_init/start`, `dsm_app_run`, cleanup |
| `ctl_main.c` | Cliente `dsmctl`: `--config/--host/--port/--timeout` + comando multi-palabra → TCP → imprime respuesta |

---

## Flujo de datos clave

### Fallo de página (cliente)
```
SIGSEGV (handler fault.c)
  → queue_push (anillo atómico)
  → resolver_loop (hilo dedicado)
    → dsm_coherence_handle_fault
      → remote_fetch (si no home)
        → call_msg (RPC PAGE_REQUEST/OWNERSHIP_REQUEST a home)
      → home: home_serve_read/write → forward/invalidate → local_install → PAGE_DATA/OWNERSHIP_GRANT
    → local_install (reintentos + coherence_make_room)
    → mprotect + touch → wake hilo usuario
```

### Escritura en página compartida
```
dsm_malloc → home OBJECT_ALLOC → page_base global
hilo usuario write → SIGSEGV (M fault)
  → OWNERSHIP_REQUEST → home
    → PAGE_FORWARD (exclusive) a owner actual
    → INVALIDATE (NEED_DATA si dirty) a sharers
    → home suelta copia → OWNERSHIP_GRANT → cliente M
```

### Expulsión (evict)
```
coherence_make_room (avoid_page)
  → coherence_evict_one
    → dsm_memory_pick_victim (min_age_ms, avoid_page)
    → si home: swap_out → local_drop
    → si cliente dirty: release_to_home (PAGE_RELEASE con datos) → local_drop
    → si cliente limpio: release_to_home (sin datos) → local_drop
  → reintenta hasta 64 víctimas; salta PEER_DOWN/TIMEOUT
```

---

## Compilación y ejecución

```bash
# En WSL2 (Alpine) o Linux nativo
cd Entregable_3/prototipo2
make                    # compila dsm_node y dsmctl
./lab.sh build          # alias con clean
./lab.sh demo           # demo en 2 nodos (nodes_2.conf, 3 barreras)
./lab.sh matrices --size 64
./lab.sh file
./lab.sh counter --iters 100 --threads 2
./lab.sh bench
./lab.sh evict          # 64 páginas, 32 marcos (2 nodos)
./lab.sh stress         # matrices 192 + counter 100×2
./lab.sh serve 0        # nodo 0 interactivo (Ctrl+C para salir)
./lab.sh ctl 0 stats    # consulta nodo 0
./lab.sh ctl 0 pages
./lab.sh ctl 0 dir
./lab.sh ctl 0 objects
./lab.sh ctl 0 nodes
./lab.sh ip 1 192.0.2.20     # cambia la IP del nodo 1 en nodes.conf y nodes_2.conf
./lab.sh show           # muestra nodes.conf + procesos
./lab.sh stop           # mata todos los dsm_node
```

Panel web (si `web_enabled=1` en config):
```
http://<host>:27303/       # HTML
http://<host>:27303/stats.json  # JSON
```

---

## Configuración (config/node0.conf..node3.conf + nodes.conf)

Parámetros clave (ver `dsm_config_t` en `dsm_config.h`):

| Parámetro | Valor típico | Descripción |
|---|---|---|
| `node_id` | 0..3 | Identificador único |
| `bind_address` | `0.0.0.0` | Interfaz de escucha |
| `listen_port` | 27300+4*id | Puerto DSM (datos) |
| `control_port` | listen+1 | Puerto `dsmctl` |
| `heartbeat_port` | listen+2 | Puerto PING/PONG |
| `web_port` | listen+3 | Puerto panel web |
| `web_enabled` | 1 | Activa panel web |
| `node_memory` | 1048576 | Bytes por nodo (1 MB) |
| `window_size` | 268435456 | Ventana virtual (256 MB) |
| `thread_pool_size` | 32 | Workers de red |
| `network_timeout_ms` | 10000 | Timeout RPC |
| `connect_timeout_ms` | 5000 | Timeout connect |
| `retry_count` | 5 | Reintentos RPC |
| `retry_backoff_ms` | 500 | Backoff lineal (ms) |
| `log_level` | 2 | 0=ERROR..4=TRACE |
| `log_file` | `logs/dsm-nodeN.log` | Archivo de log |
| `heartbeat_interval_ms` | 1000 | Periodo PING |
| `heartbeat_timeout_ms` | 3000 | Umbral nodo caído |
| `barrier_timeout_ms` | 30000 | Timeout barrera |

`nodes.conf` (id host [port]) — 4 nodos, usado por `matrices` y `serve`:
```
0 127.0.0.1 27300
1 127.0.0.1 27304
2 127.0.0.1 27308
3 127.0.0.1 27312
```

`nodes_2.conf` — misma sintaxis, solo nodos 0 y 1; lo usan `demo`, `file`, `counter`, `bench` y `evict`:
```
0 127.0.0.1 27300
1 127.0.0.1 27304
```

La barrera cuenta los nodos del archivo que recibe `--nodes`, así que ambos archivos deben tener una línea por proceso que se va a arrancar.

---

## Convenciones y reglas

1. **Comentarios en español**, identificadores `snake_case` en inglés.
2. **Locks jerárquicos:** `op_lock` (GLOBAL) → `table_lock` (OBJECT) → `page->lock` (PAGE) → `frame_lock` (FRAME). Verificado en runtime.
3. **Nunca bloquear en red con lock de página o de marco tomado.** `op_lock` sí se mantiene durante el RPC: es el serializador por página del home, y su función es justamente que dos operaciones sobre la misma página no se entrelacen.
4. **Fail-stop:** RPC fallido → `DSM_ERR_TIMEOUT`/`PEER_DOWN` → hilo usuario muere con `SIGSEGV`.
5. **Swap solo en home:** Clientes devuelven páginas sucias; home escribe a `logs/swap-nodeN.bin`.
6. **Asignación determinista:** `dsm_alloc_req_t.seq` = contador por nodo; home cachea `seq → reply` en `alloc_cache[DSM_MAX_OBJECTS]`.
7. **Barrera:** `_Atomic barrier_round/arrived` + `barrier_registered_round[node]` anti-doble-conteo.
8. **Heartbeat:** Sleep inicial 1 intervalo antes del primer PING.
9. **Salida line-buffered:** `setvbuf(stdout, NULL, _IOLBF, 0)` en `node_main.c`.
10. **Core dumps:** `ulimit -c 0` en `lab.sh` (WSL `core_pattern` pipe ignora RLIMIT_CORE).

---

## Métricas expuestas (stats)

| Campo | Significado |
|---|---|
| `page_faults` | Fallos de página resueltos |
| `local_hits` | Accesos a página residente con permisos |
| `remote_reads` | Lecturas servidas por home/owner remoto |
| `remote_writes` | Escrituras que obtuvieron ownership |
| `invalidations_sent/recv` | Contadores MSI |
| `evictions` / `evictions_dirty` | Expulsiones totales / sucias |
| `swap_bytes` | Bytes escritos a swap (home) |
| `pages_resident` / `frames_free` | Derivados (refrescados en `dsm_stats`) |
| `forwardings` | `PAGE_FORWARD` servidos por home |
| `rpc_count` / `rpc_latency_us` | RPC totales y latencia acumulada |
| `timeouts` / `errors` / `retries` | Fallos de red |
| `barriers` | Barreras completadas |
| `releases` | `PAGE_RELEASE` procesados |

---

## Limitaciones del código actual

- Home único (nodo 0) sin failover.
- Backlog `accept` fijo (128); bajo ráfaga extrema puede rechazar conexiones transitoriamente (visto con `evict` de 4 nodos y 512 páginas).
- `evict` de 4 nodos con 512 páginas no es reproducible al 100 %: hay timeouts de red en el pico de `PAGE_RELEASE`. La configuración estable es 2 nodos con 64 páginas y 32 marcos.
- `counter` pierde incrementos con contención alta: los dos nodos escriben en la misma página de 4 KB y el home cede la propiedad copiando su copia y soltándola, así que las escrituras que caen en esa ventana se pierden (`2000×4` dio `8000` de `16000`). Con `100×2` el resultado es determinista.
- Panel web sin autenticación ni TLS, y no verificado en Alpine (sin cliente HTTP en la imagen de pruebas).
- Sin compresión/agrupación de mensajes (1 RPC por página).
- `DSM_MAX_NODES=16` hardcodeado.
- `dsm_malloc` no soporta `free` de sub-rangos (solo objeto completo).

---

## Próximos pasos (entrega final)

1. Corregir `counter` con alta contención: una página de 4 KB por nodo (ranuras en `page_base + rank * page_size`) y revisar la cesión de propiedad del home (copiar bajo protección de escritura antes de soltar la copia).
2. Estabilizar `evict` de 4 nodos con 512 páginas (backlog de `accept`, reconexión en segundo plano o límite de ráfaga).
3. Elección de home + réplica de directorio.
4. `replication_factor` > 0 con lectura local de réplica.
5. Checkpointing periódico de páginas sucias.
6. Agrupación `INVALIDATE_BATCH` + prefetch.
7. Panel web con gráficos (Chart.js) + histórico.
8. Pruebas en 2+ máquinas físicas (`./lab.sh ip`).
9. Benchmarks comparativos (speedup vs local/MPI).
10. Documentación API (Doxygen/man pages).

---

*Fin de la guía.*