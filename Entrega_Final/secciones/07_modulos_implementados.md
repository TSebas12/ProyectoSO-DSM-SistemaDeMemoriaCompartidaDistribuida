# 7. Módulos implementados

## 7.1 Núcleo DSM

| Módulo | Archivo(s) | Responsabilidad | Funciones clave |
|---|---|---|---|
| Tipos y errores | `dsm_types.h` | Tipos base, códigos de error, constantes | `dsm_status_t`, `DSM_MAX_NODES`, `DSM_MAGIC` |
| Configuración | `dsm_config.h` / `config.c` | Carga, validación y guardado de `dsm_config_t` | `dsm_config_load`, `dsm_config_validate`, `dsm_config_save` |
| Nodos | `dsm_nodes.h` / `nodes.c` | Tabla de peers (id, host, port) | `dsm_nodes_load`, `dsm_nodes_get` |
| Memoria | `dsm_memory.h` / `memory.c` | Ventana mmap, tabla hash de páginas, marcos, swap, LRU | `dsm_memory_init`, `dsm_memory_pick_victim`, `dsm_memory_swap_out/in` |
| Sincronización | `dsm_sync.h` / `sync.c` | Mutex/cond con clases y verificación de orden | `dsm_mutex_init(klass)`, `dsm_cond_wait` |
| Log | `dsm_log.h` / `log.c` | Logging por niveles a archivo/stderr | `DSM_LOG(level, fmt...)` |
| Estadísticas | `dsm_stats.h` / `stats.c` | Contadores `_Atomic` + snapshot | `dsm_stats_inc`, `dsm_stats_snapshot` |
| Fallos | `dsm_fault.h` / `fault.c` | Handler `SIGSEGV`, cola lock-free, contexto por hilo | `dsm_fault_install`, `dsm_fault_attach_thread` |
| Coherencia | `dsm_coherence.h` / `coherence.c` | Núcleo MSI: directorio, ops, barrera, objetos, local_drop | `dsm_coherence_handle_fault`, `dsm_barrier` |
| Red | `dsm_net.h` / `net.c` | Transporte TCP persistente + pool de 32 workers | `dsm_net_start`, `dsm_net_send`, `dsm_net_call` |
| API | `dsm_api.h` / `dsm_api.c` | API público: malloc/free/barrier/stats/attach | `dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_stats` |
| Runtime | `dsm_runtime.h` / `runtime.c` | Orquestador de subsistemas | `dsm_runtime_init`, `dsm_runtime_start_services` |

## 7.2 Servicios y operación

| Módulo | Archivo(s) | Responsabilidad | Funciones clave |
|---|---|---|---|
| Control | `dsm_control.h` / `control.c` | Servidor TCP de texto para `dsmctl` | `dsm_control_start/stop/dispatch` |
| Web | `dsm_web.h` / `web.c` | Panel HTTP de solo lectura (`/`, `/stats.json`) | `dsm_web_start/stop/render_html` |
| Heartbeat | `dsm_heartbeat.h` / `heartbeat.c` | PING/PONG periódico, detección de nodos caídos | `dsm_heartbeat_start/stop` |
| Protocolo | `dsm_protocol.h` / `protocol.c` | Cabecera de 64 B, serialización, cargas estructuradas | `dsm_header_encode/decode`, `dsm_protocol_encode/decode` |

## 7.3 Aplicaciones de prueba

| Módulo | Archivo | Qué implementa |
|---|---|---|
| Dispatcher | `apps.c` | `dsm_app_run` despacha por nombre; valores por defecto |
| Comunes | `app_common.c` | `dsm_app_split_start/count`, `dsm_app_checksum` |
| Demo | `demo_page.c` | Demo mínima de una página compartida (E2) |
| Matrices | `app_matrices.c` | Multiplicación por bloques de filas; verifica `C = A×B` |
| Archivo | `app_file.c` | Checksum distribuido de un archivo o patrón sintético |
| Contador | `app_counter.c` | Incrementos concurrentes con falsos compartidos |
| Benchmark | `app_bench.c` | Latencia y throughput de páginas (local/remota, lectura/escritura) |
| Expulsión | `app_evict.c` | Presión de memoria para forzar LRU y swap |
| Chat | `app_chat.c` | Texto libre entre nodos (entrega final) |

## 7.4 Operación

| Módulo | Archivo | Qué implementa |
|---|---|---|
| Binario nodo | `node_main.c` | `main`: parse args, runtime, app, cleanup |
| Cliente CLI | `ctl_main.c` | `dsmctl`: `--config/--host/--port/--timeout` + comando |
| Menú | `dsm.sh` | Configuración de IPs/rol, build, arranque, apps, control, logs, parada, diagnóstico |
| Lab | `lab.sh` | build, demo, apps, control, IP, show, stop (loopback) |
| Build | `Makefile`, `build.sh` | Compilación con `gcc -std=c11` |

## 7.5 Flujo de datos clave

### Fallo de página (cliente)

```
SIGSEGV (handler fault.c)
  → SEGV_ACCERR → cola anillo → sigsuspend
  → resolver despierta con pthread_kill(DSM_SIG_RESUME)
  → dsm_coherence_handle_fault
     → local_fetch o remote_fetch
     → instalación local + reanudación
```

### Escritura en página compartida

```
Escritura → SIGSEGV → resolver
  → home_serve_write (en el home)
     → forward_from_owner (si el owner no es el home)
     → invalidación de sharers
     → OWNERSHIP_GRANT al requester
     → local_drop en el home
```

### Expulsión (evict)

```
coherence_make_room
  → pick_victim (LRU)
  → si sucia: swap_out al home
  → PAGE_RELEASE al home
  → liberar marco
```

---

*Fin de la sección 7.*
