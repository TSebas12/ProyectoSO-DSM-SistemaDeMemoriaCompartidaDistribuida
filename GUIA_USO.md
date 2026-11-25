# Guía de Uso Completa — DSM (Entrega Final)

> Documento profesional y exhaustivo para configurar, operar, comprender y verificar el Sistema de Memoria Compartida Distribuida (DSM). Incluye arquitectura interna, módulos de código con walkthrough detallado, configuración completa, pruebas y solución de problemas.

---

## 1. Introducción al sistema

El Sistema de Memoria Compartida Distribuida (DSM) permite que múltiples procesos ejecutándose en máquinas separadas compartan un espacio de memoria virtual unificado. La coherencia se mantiene mediante el protocolo MSI (Modified, Shared, Invalid) con paginación bajo demanda.

### 1.1 Características principales

- **Memoria compartida transparente:** la aplicación usa punteros normales (`int *p = dsm_malloc(...)`); el sistema resuelve fallos de página automáticamente vía `SIGSEGV`.
- **Coherencia MSI:** estados Modified (escritura exclusiva), Shared (lectura compartida), Invalid (no residente).
- **Paginación bajo demanda:** las páginas solo se traen cuando se acceden por primera vez.
- **Expulsión LRU con swap:** cuando el pool de marcos se agota, se expulsan páginas (swap en el home, liberación en clientes).
- **Fail-stop:** si un nodo cae, las operaciones que lo necesitan fallan con `DSM_ERR_TIMEOUT` (no se cuelga).
- **Heartbeat:** detección de nodos vivos cada 1 s, timeout 3 s.
- **Panel web:** solo lectura, HTML + JSON.
- **CLI de control:** `dsmctl` para consultar stats, páginas, directorio, nodos, config.
- **8 aplicaciones de prueba:** demo, matrices, file, counter, bench, evict, chat, task, stress.

### 1.2 Requisitos del sistema

- Linux (x86_64), kernel con `memfd_create` y `mmap`.
- `gcc` >= 9.0 (C11), `make` (GNU Make), `bash` >= 4.
- Bibliotecas: `-lpthread -lm -lrt`.
- Red entre nodos (loopback para pruebas locales; Ethernet/WiFi para laboratorio).
- Puertos: 27300–27399 (pareja #3).

### 1.3 Estructura del repositorio

```
ProyectoSO/
├── Entregable_3/prototipo2/
│   ├── src/                    # 30+ archivos .c
│   ├── include/                # 30+ headers
│   ├── config/                 # node0-3.conf, nodes*.conf
│   ├── Makefile
│   ├── dsm.sh                  # Menú interactivo
│   └── lab.sh                  # Atajos de prueba
├── Entrega_Final/
│   ├── 04_Entrega_Final.md     # Informe completo (21 secciones)
│   ├── secciones/              # 21 secciones individuales
│   ├── INSTRUCCIONES.md        # Instrucciones de entrega
│   └── PLAN_TRABAJO.md         # Plan de trabajo
├── README.md
├── GUIA_USO.md                 # Esta guía (fuente)
└── LAB_PRUEBAS.md              # Guía de pruebas en lab
```

---

## 2. Arquitectura del sistema

### 2.1 Visión general

El sistema se compone de N nodos. El nodo 0 es el **home**: mantiene el directorio de páginas (quién es dueño de cada página, en qué estado está, quiénes la comparten). Los demás nodos son **clientes** que solicitan páginas al home.

### 2.2 Componentes por nodo

| # | Componente | Archivo(s) | Función principal |
|---|---|---|---|
| 1 | Memoria virtual | `memory.c/h` | `mmap` ventana (PROT_NONE), tabla de páginas hash, pool de marcos, swap |
| 2 | Coherencia MSI | `coherence.c/h` | Directorio, transiciones M/S/I, invalidación, fetch, expulsión, barrera |
| 3 | Red | `net.c/h` | Sockets TCP, pool de workers, RPC, huella de config |
| 4 | Protocolo | `protocol.c/h` | Cabecera 64 B, FNV-1a, serialización |
| 5 | Fallos de página | `fault.c/h` | Manejador SIGSEGV, cola de fallos, reanudación |
| 6 | Runtime | `runtime.c/h` | Inicialización, servicios, ciclo de vida, resolver |
| 7 | API pública | `dsm_api.c/h` | `dsm_malloc`, `dsm_barrier`, `dsm_release`, etc. |
| 8 | Configuración | `config.c/h` | Parseo, validación, huella |
| 9 | Nodos | `nodes.c/h` | Parseo de tabla de nodos |
| 10 | Control | `control.c/h` | Servidor TCP para dsmctl |
| 11 | Web | `web.c/h` | Panel HTTP solo lectura |
| 12 | Heartbeat | `heartbeat.c/h` | PING/PONG, detección de nodos |
| 13 | Sync | `sync.c/h` | Mutex/cond/semáforos con verificación de orden |
| 14 | Stats | `stats.c/h` | Contadores de rendimiento |
| 15 | Log | `log.c/h` | Logging a archivo |
| 16 | Tipos | `dsm_types.h` | Enums, structs, constantes |
| 17 | Apps | `apps.c`, `app_*.c` | Aplicaciones de prueba |
| 18 | Demo | `demo_page.c` | Demostración de página |
| 19 | Main | `node_main.c` | Binario del nodo |
| 20 | ctl_main | `ctl_main.c` | Cliente de control |

### 2.3 Modelo de memoria (detalle)

- **Ventana:** `mmap(NULL, window_size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)`. Sin permisos → cualquier acceso genera SIGSEGV.
- **Backing:** `memfd_create("dsm-backing", MFD_CLOEXEC)` + `ftruncate(window_size)` + `mmap(PROT_READ|PROT_WRITE)`. Siempre escribible (el resolver usa esta vista).
- **Tabla de páginas:** hash table con `DSM_PAGE_TABLE_BUCKETS` buckets. Cada entrada tiene metadatos MSI, marco, residencia, lock, cond.
- **Marcos:** pool de `node_memory / page_size` marcos. Se asignan con `dsm_memory_alloc_frame` y se liberan con `dsm_memory_release_frame`.
- **Swap (solo home):** archivo `logs/swap-nodeN.bin`. El home escribe páginas expulsadas al swap y las restaura al necesitarlas.

### 2.4 Flujo de una operación de memoria

```
App accede a *(ptr) en la ventana
        │
        ▼
   SIGSEGV (PROT_NONE)
        │
        ▼
  dsm_segv_handler (fault.c)
  - Registra fallo en cola circular
  - Bloquea SIGUSR1
  - sigsuspend (espera)
        │
        ▼
  Hilo resolver (runtime.c)
  - queue_pop (consume fallo)
  - dsm_coherence_handle_fault:
    - Si FETCHING/EVICTING → cond_wait (con timeout)
    - Si RESIDENT y estado permite → hit (OK)
    - Si no → FETCHING, fetch, instalar, mprotect, RESIDENT
  - dsm_fault_wake → pthread_kill(SIGUSR1)
        │
        ▼
  App se reanuda (SIGUSR1 handler vacío)
  - Acceso a la página ahora tiene permisos correctos
```

### 2.5 Coherencia MSI (detalle)

**Home (nodo 0):** mantiene el directorio (`dsm_dir_entry_t`) con:
- `page_id`: ID global de la página.
- `owner_node`: nodo que tiene la copia M (o home).
- `state`: M/S/I del directorio.
- `sharers`: bitmap de nodos con copia S.
- `version`: contador de versiones.
- `last_update_ms`: timestamp.

**Transiciones:**
- **Lectura (PAGE_REQUEST):** si owner != home → forward al owner. Si owner == home → ensure_data, state=S, sharers |= requester, degradar copia del home a S.
- **Escritura (OWNERSHIP_REQUEST):** si owner != home → forward. Si owner == home → ensure_data, invalidar sharers, conceder M al requester, local_drop (home suelta su copia).
- **Invalidación (INVALIDATE):** el nodo descarta su copia, responde ACK.
- **Release (PAGE_RELEASE):** un nodo suelta su copia M, envía datos al home. El home instala los datos, invalida sharers, se queda como owner con S.

**Coherencia del home:** cuando el home sirve una lectura, su propia copia se degrada a S (`local_install(NULL, S)`) para que la próxima escritura pase por el protocolo e invalide a los demás.

### 2.6 Red y protocolo

**Cabecera (64 bytes):**

| Offset | Size | Campo |
|---|---|---|
| 0 | 8 | request_id |
| 8 | 8 | page_id |
| 16 | 4 | magic (DSM_MAGIC) |
| 20 | 2 | version (DSM_PROTOCOL_VERSION) |
| 22 | 2 | type (DSM_MSG_*) |
| 24 | 4 | flags |
| 28 | 4 | header_len (64) |
| 32 | 4 | payload_len |
| 36 | 4 | src_node |
| 40 | 4 | dst_node |
| 44 | 4 | status |
| 48 | 4 | checksum (FNV-1a) |
| 52 | 4 | payload_off (64) |
| 56 | 2 | ttl |
| 58 | 2 | reserved |
| 60 | 4 | reserved2 (huella de config) |

**Tipos de mensaje:** PING, PONG, PAGE_REQUEST, PAGE_DATA, OWNERSHIP_REQUEST, OWNERSHIP_GRANT, PAGE_FORWARD, INVALIDATE, INVALIDATE_ACK, PAGE_WRITEBACK, PAGE_WRITEBACK_ACK, PAGE_RELEASE, ACK, OBJECT_ALLOC, OBJECT_ALLOC_REPLY, BARRIER, BARRIER_REPLY, PAGE_LIST, PAGE_LIST_REPLY, STATS_REQUEST, STATS_REPLY, ERROR.

**Huella de configuración:** FNV-1a de `config_version, page_size, node_memory, window_size, max_nodes, port_range_*, replication_factor, max_message_size, heartbeat_*, network_timeout, connect_timeout, thread_pool_size, barrier_timeout, DSM_PROTOCOL_VERSION` + tabla de nodos (ordenada por id). Si dos nodos tienen huellas distintas, `DSM_ERR_CONFIG_MISMATCH`.

**Pool de workers:** `thread_pool_size` hilos atienden conexiones entrantes persistentes. Cada conexión se atiende en serie por un worker.

---

## 3. Configuración completa

### 3.1 Archivo `config/nodeN.conf`

Todas las claves con valores por defecto (nodo 0):

```
config_version = 1
node_id = 0
max_nodes = 16
bind_address = 0.0.0.0
listen_port = 27300
control_port = 27301
heartbeat_port = 27302
web_port = 27303
web_enabled = 1
seed_host = 127.0.0.1
seed_port = 27300
page_size = 4096
node_memory = 1048576
window_size = 268435456
heartbeat_interval_ms = 1000
heartbeat_timeout_ms = 3000
network_timeout_ms = 10000
connect_timeout_ms = 5000
retry_count = 5
retry_backoff_ms = 500
max_message_size = 8192
thread_pool_size = 32
fault_queue_size = 1024
eviction_policy = 0
log_level = 2
log_file = logs/dsm-node0.log
stats_interval_ms = 5000
barrier_timeout_ms = 30000
replication_factor = 0
checkpoint_interval_ms = 0
scheduler_policy = 0
port_range_start = 27300
port_range_end = 27399
```

### 3.2 Tabla de claves de configuración

| Clave | Tipo | Descripción | Rango válido |
|---|---|---|---|
| `config_version` | uint32 | Versión del formato | 1 |
| `node_id` | uint32 | ID del nodo (0 = home) | 0..max_nodes-1 |
| `max_nodes` | uint32 | Máximo de nodos | 1..16 |
| `bind_address` | string | IP de escucha | IP válida o `0.0.0.0` |
| `listen_port` | uint16 | Puerto DSM | port_range_start..end |
| `control_port` | uint16 | Puerto de control (dsmctl) | port_range_start..end |
| `heartbeat_port` | uint16 | Puerto de heartbeat | port_range_start..end |
| `web_port` | uint16 | Puerto del panel web | port_range_start..end |
| `web_enabled` | uint16 | Habilitar panel web | 0 o 1 |
| `seed_host` | string | IP del home (nodo 0) | IP válida |
| `seed_port` | uint16 | Puerto DSM del home | port_range_start..end |
| `page_size` | uint32 | Tamaño de página (potencia de 2) | ≥4096 |
| `node_memory` | uint64 | Memoria del nodo en bytes | ≥16 páginas |
| `window_size` | uint64 | Ventana de memoria compartida | ≥node_memory |
| `heartbeat_interval_ms` | uint32 | Intervalo de heartbeat | — |
| `heartbeat_timeout_ms` | uint32 | Timeout de heartbeat | ≥2×interval |
| `network_timeout_ms` | uint32 | Timeout de RPC | — |
| `connect_timeout_ms` | uint32 | Timeout de conexión | — |
| `retry_count` | uint32 | Reintentos de RPC | 0..10 |
| `retry_backoff_ms` | uint32 | Backoff entre reintentos | — |
| `max_message_size` | uint32 | Tamaño máximo de mensaje | page_size+256..64MB |
| `thread_pool_size` | uint32 | Hilos de red | 1..64 |
| `fault_queue_size` | uint32 | Cola de fallos de página | — |
| `eviction_policy` | uint32 | 0=LRU, 1=Clock | 0 o 1 |
| `log_level` | uint32 | 0=off, 1=error, 2=warn, 3=info, 4=debug | 0..4 |
| `log_file` | string | Ruta del log | — |
| `stats_interval_ms` | uint32 | Intervalo de stats | — |
| `barrier_timeout_ms` | uint32 | Timeout de barrera | — |
| `replication_factor` | uint32 | Factor de réplica (Extra 2, no implementado) | 0..max_nodes-1 |
| `checkpoint_interval_ms` | uint32 | Intervalo de checkpoint | — |
| `scheduler_policy` | uint32 | 0=round_robin, 1=least_loaded | — |
| `port_range_start` | uint32 | Inicio del rango de puertos | — |
| `port_range_end` | uint32 | Fin del rango de puertos | — |

### 3.3 Tabla de nodos `config/nodes_N.conf`

Formato: `id host puerto` (una línea por nodo).

```
0 127.0.0.1 27300
1 127.0.0.1 27304
2 127.0.0.1 27308
3 127.0.0.1 27312
```

Puertos: `27300 + 4*node_id`. Rango: 27300–27399 (pareja #3).

### 3.4 Argumentos CLI de `dsm_node`

| Argumento | Descripción | Tipo |
|---|---|---|
| `--config RUTA` | Archivo de config del nodo | string |
| `--nodes RUTA` | Tabla de nodos | string |
| `--app NOMBRE` | demo, matrices, file, counter, bench, evict, chat, task, stress | string |
| `--size N` | Tamaño de matriz o bytes de archivo | uint32 |
| `--pages N` | Páginas para bench/evict | uint32 |
| `--iters N` | Iteraciones | uint32 |
| `--threads N` | Hilos por nodo (counter) | uint32 |
| `--file RUTA` | Archivo para app file | string |
| `--nick NOMBRE` | Nick para chat | string |
| `--text TEXTO` | Texto inicial para chat | string |
| `--duration S` | Duración en segundos (chat) | uint32 |
| `--frames N` | Marcos por nodo (evict) | uint32 |
| `--no-verify` | Omitir verificación | flag |
| `--serve` | Quedar escuchando después de la app | flag |
| `--check-config` | Validar config y salir (imprime huella) | flag |
| `--version` | Versión | flag |

### 3.5 `dsmctl` (cliente de control)

```bash
./dsmctl --config config/node0.conf stats    # estadísticas
./dsmctl --config config/node0.conf pages    # páginas locales
./dsmctl --config config/node0.conf dir      # directorio (solo home)
./dsmctl --config config/node0.conf nodes    # nodos + heartbeat
./dsmctl --config config/node0.conf config   # huella de configuración
./dsmctl --host IP --port PUERTO stats       # conexión remota
./dsmctl --timeout MS ...                    # timeout personalizado
```

**Comandos disponibles:** stats, pages, dir, objects, nodes, info, ping, config, help.

### 3.6 Panel web

Si `web_enabled=1`:
- `http://HOST:WEB_PORT/` → HTML con estado del nodo.
- `http://HOST:WEB_PORT/stats.json` → JSON con estadísticas.

### 3.7 `dsm.sh` — menú interactivo

| Opción | Función |
|---|---|
| 1 | Configurar topología (2/3/4 nodos) e IPs |
| 2 | Elegir rol de esta máquina (A/B/C/D) |
| 3 | Compilar |
| 4 | Arrancar nodo (escuchando) |
| 5 | Arrancar nodo con app |
| 6 | Consultar stats |
| 7 | Consultar páginas |
| 8 | Detener todo |
| 9 | Diagnóstico de red |
| 10 | Mostrar estado |
| 11 | Validar configuración |

---

## 4. Módulos de código (descripción detallada)

### 4.1 `memory.c` — Memoria virtual

- `dsm_memory_init`: crea `mmap` (PROT_NONE) y `memfd_create` (backing).
- `dsm_memory_page_get_or_create`: busca o crea entrada en hash table.
- `dsm_memory_alloc_frame`: reserva un marco del pool.
- `dsm_memory_release_frame`: libera un marco.
- `dsm_memory_protect`: `mprotect` según estado MSI (M→RW, S→R, I→NONE).
- `dsm_memory_copy_in/out`: copia datos entre ventana y buffer (vía backing).
- `dsm_memory_swap_out/in`: escribe/lee el archivo de swap.
- `dsm_memory_pick_victim`: selecciona víctima LRU para expulsión.
- `dsm_memory_addr_to_page`: traduce dirección de ventana a page_id.

### 4.2 `coherence.c` — Coherencia MSI (~1800 líneas)

- `dsm_coherence_init`: inicializa directorio, objetos, barrera, locks.
- `dsm_coherence_handle_fault`: resuelve fallo de página (fetch + instalar + publicar permisos).
- `home_serve_read`: sirve lectura (forward si no es home, o ensure_data + democión a S).
- `home_serve_write`: sirve escritura (forward si no es home, o ensure_data + invalidar sharers + conceder M + local_drop).
- `handle_invalidate`: descarta copia local y confirma.
- `handle_page_release`: recibe página liberada, instala datos, invalida sharers, actualiza directorio.
- `dsm_coherence_alloc`: reserva objeto (monótono, determinista por seq).
- `dsm_coherence_barrier`: barrera centralizada por sondeo.
- `dsm_coherence_release_page`: publica y suelta una página M (usado por chat y task).
- `dsm_coherence_force_eviction`: fuerza expulsión de N páginas.
- `forward_from_owner`: pide página al dueño actual (PAGE_FORWARD).
- `invalidate_node`: envía INVALIDATE a un nodo y espera ACK.
- `local_drop`: descarta copia local (libera marco).
- `home_ensure_data`: garantiza que el home tenga los datos (materializa ceros si nunca tocada, restaura de swap).

### 4.3 `fault.c` — Manejador de SIGSEGV

- `dsm_segv_handler`: registra el fallo en la cola circular (productor).
- `dsm_fault_queue_pop`: consume fallos (consumidor único, con timeout).
- `dsm_fault_wake`: reanuda el hilo (`pthread_kill(SIGUSR1)`).
- `dsm_fault_mark_failed`: marca el hilo como fallido (fail-stop).
- Cola circular con semáforo (`dsm_sem_t items`).
- Pila alternativa (`sigaltstack`) para evitar stack overflow en el handler.

### 4.4 `net.c` — Red

- `dsm_net_create/start/stop/destroy`: ciclo de vida.
- `peer_acquire/release`: conexión persistente por peer con lock (serializa RPCs).
- `rpc/rpc_once`: envío y recepción de RPC (con reintentos).
- `serve_connection`: atiende conexión persistente (worker).
- `listener_loop`: acepta conexiones, encola en `queue`.
- `dsm_net_ping`: ping con timeout.
- Huella de config en `reserved2` (offset 60 de la cabecera).
- Pool de workers: `thread_pool_size` hilos.

### 4.5 `protocol.c` — Protocolo

- `dsm_header_encode/decode`: serialización de 64 B (htonl/ntohl).
- `dsm_protocol_encode/decode`: mensaje completo (cabecera + payload).
- `dsm_checksum_fnv1a`: FNV-1a de la carga (se verifica siempre, incluso para payload vacío).

### 4.6 `config.c` — Configuración

- `dsm_config_load`: parsea `nodeN.conf` (clave = valor, comentarios con #).
- `dsm_config_validate`: valida rangos y coherencia (puertos, page_size, node_memory, etc.).
- `dsm_config_fingerprint`: FNV-1a de parámetros + tabla de nodos.

### 4.7 `nodes.c` — Tabla de nodos

- `dsm_nodes_load`: parsea `nodes_N.conf` (id host puerto).
- `dsm_nodes_print`: imprime la tabla.

### 4.8 `runtime.c` — Runtime

- `dsm_runtime_init`: inicializa memoria, fault, coherencia, red, resolver.
- `dsm_runtime_start_services`: control, web, heartbeat.
- `dsm_runtime_destroy`: apagado ordenado (web → control → heartbeat → net → coherence → memory).
- `resolver_loop`: hilo que resuelve fallos de página (con reintento ×5).

### 4.9 `control.c` / `web.c` — Servicios

- **Control:** TCP, recibe comando de texto, responde texto. Sin autenticación (laboratorio). Comandos: stats, pages, dir, objects, nodes, info, ping, config, help.
- **Web:** HTTP GET `/` (HTML) y `/stats.json` (JSON). Solo lectura.

### 4.10 `heartbeat.c` — Detección de nodos

- Hilo que recorre peers, envía PING, registra alive/last_seen.
- `dsm_heartbeat_alive`: consulta estado.
- `dsm_heartbeat_format`: formatea para `dsmctl nodes`.

### 4.11 `sync.c` — Sincronización

- `dsm_mutex_t`, `dsm_cond_t`, `dsm_sem_t`: wrappers sobre pthread.
- Verificación de orden de locks (`push_class`/`pop_class`) para prevenir deadlocks.
- `dsm_sync_thread_reset`: resetea el stack de locks del hilo.

### 4.12 `stats.c` — Estadísticas

Contadores: page_faults, local_hits, invalidations_sent/received, evictions, evictions_dirty, swap_bytes, timeouts, errors, messages_sent/received, bytes_sent/received, uptime.

---

## 5. Aplicaciones de prueba (detalle)

### 5.1 demo (`demo_page.c`)

Demostración de una página compartida: el nodo 0 escribe un valor, los demás lo leen. 3 barreras para sincronizar.

### 5.2 matrices (`app_matrices.c`)

Multiplicación de matrices por bloques. Cada nodo calcula un bloque de filas de C = A × B. Verificación independiente de A, B y C contra fórmulas deterministas.

- **Entrada:** `--size N` (tamaño de la matriz, default 64).
- **Salida:** `verificacion: OK (0 discrepancias)`.
- **Código de salida:** 0 si OK, 2 si discrepancia.

### 5.3 file (`app_file.c`)

Checksum distribuido de un archivo (o patrón sintético de 1 MB). Cada nodo calcula un checksum parcial de su bloque. El nodo 0 verifica todos los parciales contra el buffer compartido.

- **Entrada:** `--file RUTA` (opcional, default sintético).
- **Salida:** `checksum completo = 0xc2dc9dc5, OK`.
- **Si el archivo no existe:** `abortado (archivo no disponible)`, exit ≠ 0.

### 5.4 counter (`app_counter.c`)

Incrementos concurrentes: cada hilo incrementa su propia ranura (sin falsos compartidos). Lee la ranura del nodo vecino para provocar invalidaciones.

- **Entrada:** `--iters N --threads N`.
- **Salida:** `suma = X, esperado = Y, OK`.

### 5.5 bench (`app_bench.c`)

Mide latencia de: lectura local, lectura remota, escritura remota. Usa páginas que pertenecen a otros nodos para forzar tráfico de red.

- **Entrada:** `--iters N`.
- **Salida:** latencias en us/op por nodo.

### 5.6 evict (`app_evict.c`)

Fuerza expulsiones/swap escribiendo más páginas que marcos disponibles. Primera pasada: cada nodo escribe sus páginas. Segunda pasada: todos leen todas. Verifica que los bytes sobrevivan.

- **Entrada:** `--pages N --frames M`.
- **Salida:** `verificacion OK`, expulsiones > 0, swap > 0.

### 5.7 chat (`app_chat.c`)

Chat de texto libre entre nodos. Cada nodo escribe en su propia página. Tras cada mensaje se llama `dsm_release()` para publicar. Los demás nodos sondean y muestran mensajes nuevos.

- **Entrada:** `--nick NOMBRE --text TEXTO --duration S --serve`.
- **Comandos:** `/nick`, `/help`, `/quit`.
- **Salida:** mensajes cruzan entre nodos.

### 5.8 task (`app_task.c`) — Extra 1

Ejecución distribuida: el coordinador (nodo 0) prepara un arreglo compartido. Cada nodo procesa su porción en paralelo (suma de cuadrados). El coordinador recolecta, verifica contra referencia secuencial, reporta speedup y balance.

- **Entrada:** `--size N` (default 1 000 000).
- **Salida:** `speedup=X.XX, balance=Y.YY, OK`.

### 5.9 stress (`apps.c`)

Combina `matrices 192` y `counter 100×2` para probar bajo carga combinada.

---

## 6. Pruebas en 1 PC (localhost)

### 6.1 Configuración

```bash
cd Entregable_3/prototipo2
./dsm.sh
# Opción 1 → 4 nodos → IP 127.0.0.1 para todos
# Opción 2 → rol A
# Opción 3 → compilar
# Opción 11 → validar (misma huella en los 4)
```

### 6.2 Ejecutar todas las apps

Ver tabla en sección 3.3 de `GUIA_PRUEBAS.md` (comandos y salidas esperadas).

### 6.3 Verificar fallos

- **Nodo caído:** `kill -9` de un proceso → los demás reportan timeout.
- **Config mismatch:** cambiar `page_size` en un nodo → error rápido.
- **File inexistente:** `--file /tmp/noexiste` → `abortado`, exit ≠ 0.

---

## 7. Pruebas en 2 PCs

### 7.1 Configuración

1. Generar topología de 2 nodos en PC A con las IPs reales.
2. Copiar `config/` a PC B.
3. Configurar rol A en PC A, rol B en PC B.
4. Validar huella en ambas.

### 7.2 Ejecutar

```bash
# PC A (home)
./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app demo --serve
# PC B
./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app demo
```

### 7.3 Verificar latencia real

```bash
./dsm_node --config config/nodeX.conf --nodes config/nodes_2.conf --app bench --iters 1000
```

Latencia remota > latencia local (red real vs loopback).

---

## 8. Pruebas en 3 o 4 PCs

Igual que en 2 PCs pero con 3 o 4 nodos. Cada PC tiene un rol (A/B/C/D) y su IP real. Las apps se ejecutan en paralelo en todos los nodos.

---

## 9. Solución de problemas

| Síntoma | Causa | Solución |
|---|---|---|
| `bind: Address already in use` | Nodo anterior no terminó | `pkill -f dsm_node` |
| `config mismatch` | Huellas distintas | Comparar `dsmctl config` |
| `no se pudo conectar` | Nodo 0 no corriendo | Arrancar nodo 0 primero |
| `Timeouts` | Barrera inicial > 60 s | Arrancar con < 2 min de diferencia |
| `make` falla | Falta build-essential | `sudo apt install build-essential` |
| Chat no muestra nada | Barrera no pasó | Revisar logs |
| `verificacion FALLO` en evict | Pérdida de datos | Revisar swap |
| `abortado (archivo no disponible)` | Archivo no existe | Verificar ruta con `--file` |
| Zombies tras Ctrl+C | `dsm_fault_shutdown` no llamado | Verificar `dsm_runtime_destroy` |
| Huellas distintas | `page_size`/`node_memory`/etc. difieren | Comparar configs con `diff` |

---

## 10. Checklist final

- [ ] Código compila sin warnings (`-Wall -Wextra -Wpedantic`).
- [ ] `dsm_node` y `dsmctl` generados.
- [ ] Configuración validada (misma huella en todos los nodos).
- [ ] Demo ejecuta sin errores.
- [ ] Matrices: `verificacion: OK`.
- [ ] File: `checksum completo = 0xc2dc9dc5, OK`.
- [ ] Counter: `suma = esperado, OK`.
- [ ] Bench: latencias impresas.
- [ ] Evict: `verificacion OK`, expulsiones > 0.
- [ ] Chat: mensajes cruzan.
- [ ] Task: `speedup > 1, balance ~1, OK`.
- [ ] Stress: OK.
- [ ] Fail-stop: nodo caído detectado.
- [ ] Config mismatch: detectado sin timeout.
- [ ] Ctrl+C: salida limpia.
- [ ] Panel web: responde.
- [ ] Informe: 21 secciones.
- [ ] Manual: completo.
- [ ] Git: commits de ambos integrantes.

---

## 11. Walkthrough de código clave

### 11.1 `coherence.c` — `dsm_coherence_handle_fault` (resolución de fallo)

```c
dsm_status_t dsm_coherence_handle_fault(dsm_coherence_t *co, const dsm_fault_req_t *req) {
    // 1. Obtener o crear entrada de página
    page = dsm_memory_page_get_or_create(co->mem, page_id, object_of_page(co, page_id));
    // 2. Lock de página
    dsm_mutex_lock(&page->lock);
    // 3. Si FETCHING/EVICTING → esperar (cond_wait con timeout)
    // 4. Si RESIDENT y estado permite el acceso → hit (OK)
    // 5. Si no → FETCHING, fetch (local o remoto), instalar, mprotect, RESIDENT
    // 6. Broadcast cond, unlock, retornar
}
```

**Puntos clave:**
- La página se queda en FETCHING durante el fetch para que otros hilos esperen.
- `dsm_memory_copy_in` escribe en el backing (siempre escribible).
- `dsm_memory_protect` publica los permisos DESPUÉS de copiar (evita ver datos a medio copiar).
- Si el fetch falla, se marca `RES_ERROR` y se despierta a los esperadores.

### 11.2 `fault.c` — Manejador SIGSEGV

```c
static void dsm_segv_handler(int sig, siginfo_t *si, void *uctx) {
    ctx = t_ctx;  // contexto del hilo (thread-local)
    if (!installed || ctx == NULL || si == NULL) { reraise; return; }
    if (si->si_code != SEGV_ACCERR) { reraise; return; }
    if (ctx->in_fault || ctx->failed) { reraise; return; }

    // Calcular page_id desde la dirección del fallo
    page_id = ...;  // traducir a page_id global

    // Registrar en la cola circular
    queue_push(&req);

    // Esperar reanudación
    sigsuspend(waitmask);  // espera
}
```

**Puntos clave:**
- Solo maneja `SEGV_ACCERR` (acceso sin permiso), no `SEGV_MAPERR`.
- Usa pila alternativa (`sigaltstack`) para evitar stack overflow.
- La cola circular es lock-free para productores (CAS en `head`).
- `in_fault` se publica con release antes de `sigsuspend`.

### 11.3 `net.c` — `serve_connection` (worker)

```c
static void serve_connection(dsm_net_t *net, int cfd) {
    active_add(net, cfd);
    while (running) {
        // Leer cabecera de 64 B
        if (dsm_net_read_full(cfd, hdr, 64) != 0) break;
        dsm_header_decode(hdr, &h);
        // Validar magic, version, payload_len
        // Verificar huella de config (reserved2)
        if (mismatch) { enviar error; break; }
        // Leer payload
        // Decodificar mensaje
        // t_reply_fd = cfd (para que la respuesta vuelva por esta conexión)
        // Despachar: PING, STATS, o coherence_handle_message
        // t_reply_fd = -1
        // Liberar buffers
    }
    active_remove(net, cfd);
}
```

**Puntos clave:**
- `t_reply_fd` es thread-local: cada worker envía respuestas por su propia conexión.
- La conexión es persistente: el bucle lee múltiples mensajes.
- En config mismatch, envía un frame de error antes de cerrar.

### 11.4 `runtime.c` — `resolver_loop`

```c
static void resolver_loop_body(dsm_runtime_t *rt) {
    for (;;) {
        dsm_fault_req_t req;
        if (dsm_fault_queue_pop(&req, UINT32_MAX) != DSM_OK) break;
        // Reintentar hasta 5 veces (absorbe carrera de handoff)
        for (attempt = 0; attempt < 5; attempt++) {
            st = dsm_coherence_handle_fault(&rt->co, &req);
            if (st == DSM_OK) break;
            if (attempt < 4) nanosleep(10ms);
        }
        if (st != DSM_OK) {
            DSM_LOG(ERROR, "no se pudo resolver...");
            dsm_fault_mark_failed(req.ctx);
        }
        dsm_fault_wake(req.ctx);
    }
}
```

**Puntos clave:**
- Un solo hilo resolver (consumidor único).
- Reintento ×5 para absorber la ventana de handoff de propiedad.
- Si falla, marca el hilo como fallido (fail-stop) y despierta al app.

---

## 12. Referencia de API pública

### 12.1 Inicialización

| Función | Firma | Descripción |
|---|---|---|
| `dsm_init` | `dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out)` | Inicializa un runtime con tabla trivial de 1 nodo |
| `dsm_shutdown` | `void dsm_shutdown(dsm_runtime_t *rt)` | Apaga el runtime |
| `dsm_attach` | `dsm_status_t dsm_attach(dsm_runtime_t *rt)` | Registra el hilo actual en SIGSEGV |
| `dsm_detach` | `void dsm_detach(dsm_runtime_t *rt)` | Desregistra el hilo |

### 12.2 Memoria compartida

| Función | Firma | Descripción |
|---|---|---|
| `dsm_malloc` | `void *dsm_malloc(dsm_runtime_t *rt, size_t size)` | Reserva páginas del espacio global |
| `dsm_calloc` | `void *dsm_calloc(dsm_runtime_t *rt, size_t count, size_t size)` | Reserva e inicializa a cero |
| `dsm_free` | `void dsm_free(dsm_runtime_t *rt, void *ptr)` | No-op (asignador monótono) |
| `dsm_addr_to_page` | `uint64_t dsm_addr_to_page(dsm_runtime_t *rt, const void *addr)` | Traduce dirección a page_id |
| `dsm_release` | `dsm_status_t dsm_release(dsm_runtime_t *rt, void *ptr, size_t len)` | Publica y suelta páginas M |

### 12.3 Sincronización

| Función | Firma | Descripción |
|---|---|---|
| `dsm_barrier` | `dsm_status_t dsm_barrier(dsm_runtime_t *rt, uint32_t timeout_ms)` | Barrera centralizada |

### 12.4 Consulta

| Función | Firma | Descripción |
|---|---|---|
| `dsm_node_id` | `uint32_t dsm_node_id(dsm_runtime_t *rt)` | ID del nodo actual |
| `dsm_node_count` | `uint32_t dsm_node_count(dsm_runtime_t *rt)` | Número de nodos |
| `dsm_page_size` | `uint32_t dsm_page_size(dsm_runtime_t *rt)` | Tamaño de página |
| `dsm_stats` | `void dsm_stats(dsm_runtime_t *rt, dsm_stats_view_t *out)` | Copia contadores |

---

## 13. Catálogo de mensajes del protocolo

| Tipo | Dirección | Descripción |
|---|---|---|
| `PING` / `PONG` | Cualquiera → Cualquiera | Heartbeat |
| `PAGE_REQUEST` | Cliente → Home | Solicitar lectura de página |
| `PAGE_DATA` | Home → Cliente | Datos de la página |
| `OWNERSHIP_REQUEST` | Cliente → Home | Solicitar propiedad de escritura |
| `OWNERSHIP_GRANT` | Home → Cliente | Concesión de propiedad + datos |
| `PAGE_FORWARD` | Home → Owner | Home pide página al dueño actual |
| `INVALIDATE` | Home → Sharer | Invalidar copia local |
| `INVALIDATE_ACK` | Sharer → Home | Confirmación de invalidación |
| `PAGE_WRITEBACK` | Cliente → Home | Enviar página sucia (compatibilidad E2) |
| `PAGE_WRITEBACK_ACK` | Home → Cliente | Confirmación de writeback |
| `PAGE_RELEASE` | Cliente → Home | Soltar copia M con datos |
| `ACK` | Home → Cliente | Confirmación genérica |
| `OBJECT_ALLOC` | Cliente → Home | Solicitar reserva de objeto |
| `OBJECT_ALLOC_REPLY` | Home → Cliente | Respuesta de reserva |
| `BARRIER` | Cliente → Home | Llegada a barrera |
| `BARRIER_REPLY` | Home → Cliente | Ronda de barrera publicada |
| `PAGE_LIST` | Cliente → Home | Listar páginas |
| `PAGE_LIST_REPLY` | Home → Cliente | Lista de páginas |
| `STATS_REQUEST` | Cliente → Nodo | Solicitar estadísticas |
| `STATS_REPLY` | Nodo → Cliente | Estadísticas |
| `ERROR` | Cualquiera → Cualquiera | Error (incluye config mismatch) |

---

## 14. Tabla de transiciones MSI

| Estado actual | Evento | Estado siguiente | Acción |
|---|---|---|---|
| I | Lectura | S | Fetch de datos, mprotect(R) |
| I | Escritura | M | Fetch de datos, mprotect(RW), is_dirty=1 |
| S | Lectura | S | (ya residente, hit) |
| S | Escritura | M | Invalidar sharers, mprotect(RW) |
| M | Lectura | M | (hit, sin cambio) |
| M | Escritura | M | (hit, sin cambio) |
| S | Invalidación | I | Descartar copia, mprotect(NONE) |
| M | Invalidación | I | Descartar copia, mprotect(NONE) |
| M | Release | (drop) | Enviar datos al home, local_drop |

---

## 15. Ejemplos de configuración para N PCs

### 15.1 1 PC (loopback, 4 nodos)

```bash
# config/node0.conf (home)
node_id = 0
listen_port = 27300
control_port = 27301
heartbeat_port = 27302
web_port = 27303
seed_host = 127.0.0.1
seed_port = 27300
# config/node1.conf (cliente)
node_id = 1
listen_port = 27304
control_port = 27305
heartbeat_port = 27306
web_port = 27307
seed_host = 127.0.0.1
seed_port = 27300
# (node2, node3 análogos con puertos +4)
```

### 15.2 2 PCs

**PC A (home, 192.168.1.10):**
```
node_id = 0
listen_port = 27300
...
seed_host = 127.0.0.1
```

**PC B (cliente, 192.168.1.20):**
```
node_id = 1
listen_port = 27304
...
seed_host = 192.168.1.10
seed_port = 27300
```

Tabla de nodos (en ambas PCs):
```
0 192.168.1.10 27300
1 192.168.1.20 27304
```

### 15.3 4 PCs

Igual que 2 PCs pero con 4 nodos. Cada PC tiene su IP real y su rol (A/B/C/D). La tabla de nodos tiene 4 líneas.

---

## 16. Rendimiento y tuning

### 16.1 Parámetros que afectan rendimiento

| Parámetro | Efecto | Recomendación |
|---|---|---|
| `node_memory` | Más marcos = menos expulsiones | 1 MB (256 marcos) para pruebas |
| `page_size` | Páginas más grandes = menos fallos | 4096 (mínimo) para más granularidad |
| `thread_pool_size` | Más hilos = más concurrencia | 32 para laboratorio |
| `network_timeout_ms` | Menor = detección más rápida | 10000 (balance) |
| `heartbeat_interval_ms` | Menor = detección más rápida | 1000 |
| `retry_count` | Más reintentos = más tolerancia | 5 |
| `eviction_policy` | LRU vs Clock | 0 (LRU) por defecto |

### 16.2 Métricas a reportar

- **Latencia de página:** us/op para lectura local, lectura remota, escritura remota.
- **Throughput:** páginas/s bajo carga.
- **Speedup:** tiempo secuencial / tiempo paralelo (app task).
- **Balance de carga:** max_time / min_time (1.0 = perfecto).
- **Expulsiones:** número de páginas expulsadas.
- **Swap:** bytes escritos al swap.
- **Invalidaciones:** sent/received.

---

## 17. Consideraciones de seguridad

- **Sin autenticación:** el sistema es para laboratorio. No exponer puertos fuera de la red de pruebas.
- **Sin cifrado:** el tráfico TCP va en claro. No enviar datos sensibles.
- **Panel web:** solo lectura, pero expone estadísticas y tablas de páginas. Limitar acceso por firewall.
- **Huella de config:** detecta configuraciones incompatibles pero no previene ataques.
- **Fail-stop:** si un nodo cae, las operaciones fallan (no hay recuperación automática).
- **Datos sensibles:** no versionar IPs reales, carnés, nombres de profesor/institución. Se limpiará en la versión final.

---

## 18. Extras

### 18.1 Extra 1 — Ejecución distribuida (IMPLEMENTADO)

- **App:** `task` (`--app task --size N`).
- **Modelo:** coordinador (nodo 0) + trabajadores. Cada nodo procesa su porción en paralelo.
- **Resultados:** speedup, balance de carga, verificación contra referencia secuencial.
- **Métricas:** speedup > 1.0, balance cercano a 1.0.

### 18.2 Extra 2 — Tolerancia a fallos (NO IMPLEMENTADO)

- **Diseño documentado:** réplicas de páginas M, `REPLICA_WRITE`/`REPLICA_ACK`, promoción de réplica, checkpoints.
- **Estado:** diseño documentado, no implementado. `replication_factor` existe en config pero no se usa.
- **Motivo:** demasiado invasivo para el tiempo disponible; una implementación parcial sería peor que no tenerla.

---

## 19. Solución de problemas

| Síntoma | Causa | Solución |
|---|---|---|
| `bind: Address already in use` | Nodo anterior no terminó | `pkill -f dsm_node` |
| `config mismatch` | Huellas distintas | Comparar `dsmctl config` |
| `no se pudo conectar` | Nodo 0 no corriendo | Arrancar nodo 0 primero |
| `Timeouts` | Barrera inicial > 60 s | Arrancar con < 2 min de diferencia |
| `make` falla | Falta build-essential | `sudo apt install build-essential` |
| Chat no muestra nada | Barrera no pasó | Revisar logs |
| `verificacion FALLO` en evict | Pérdida de datos | Revisar swap |
| `abortado (archivo no disponible)` | Archivo no existe | Verificar ruta con `--file` |
| Zombies tras Ctrl+C | `dsm_fault_shutdown` no llamado | Verificar `dsm_runtime_destroy` |
| Huellas distintas | `page_size`/`node_memory`/etc. difieren | Comparar configs con `diff` |
| `SIGSEGV` sin resolver | Página fuera de la ventana | Verificar que el puntero esté en la ventana |
| `DSM_ERR_NOMEM` | Sin marcos disponibles | Aumentar `node_memory` o reducir páginas |
| `DSM_ERR_TIMEOUT` | Nodo caído o red lenta | Verificar heartbeat, aumentar timeout |

---

## 20. Checklist final

- [ ] Código compila sin warnings (`-Wall -Wextra -Wpedantic`).
- [ ] `dsm_node` y `dsmctl` generados.
- [ ] Configuración validada (misma huella en todos los nodos).
- [ ] Demo ejecuta sin errores.
- [ ] Matrices: `verificacion: OK`.
- [ ] File: `checksum completo = 0xc2dc9dc5, OK`.
- [ ] Counter: `suma = esperado, OK`.
- [ ] Bench: latencias impresas.
- [ ] Evict: `verificacion OK`, expulsiones > 0.
- [ ] Chat: mensajes cruzan.
- [ ] Task: `speedup > 1, balance ~1, OK`.
- [ ] Stress: OK.
- [ ] Fail-stop: nodo caído detectado.
- [ ] Config mismatch: detectado sin timeout.
- [ ] Ctrl+C: salida limpia.
- [ ] Panel web: responde.
- [ ] Informe: 21 secciones.
- [ ] Manual: completo.
- [ ] Git: commits de ambos integrantes.

---

## 21. Detalle de implementación: manejo de memoria

### 21.1 Inicialización de la memoria (`memory.c`)

```c
dsm_status_t dsm_memory_init(dsm_memory_t *mem, const dsm_config_t *cfg) {
    // 1. Calcular número de marcos
    mem->page_count = cfg->window_size / cfg->page_size;
    mem->frame_count = cfg->node_memory / cfg->page_size;

    // 2. Crear ventana mmap (PROT_NONE)
    mem->window_base = mmap(NULL, cfg->window_size, PROT_NONE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // 3. Crear backing store (memfd)
    mem->backing_fd = memfd_create("dsm-backing", MFD_CLOEXEC);
    ftruncate(mem->backing_fd, cfg->window_size);
    mem->backing_base = mmap(NULL, cfg->window_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, mem->backing_fd, 0);

    // 4. Inicializar tabla de páginas (hash)
    for (i = 0; i < DSM_PAGE_TABLE_BUCKETS; i++)
        mem->buckets[i] = NULL;

    // 5. Inicializar pool de marcos
    for (i = 0; i < mem->frame_count; i++)
        mem->frames[i].state = DSM_FRAME_FREE;
}
```

### 21.2 Traducción de dirección a page_id

```c
uint64_t dsm_memory_addr_to_page(const dsm_memory_t *mem, const void *addr) {
    uintptr_t base = (uintptr_t)mem->window_base;
    uintptr_t a = (uintptr_t)addr;
    if (a < base || a >= base + mem->window_size) return DSM_INVALID_PAGE_ID;
    return mem->page_base_id + (a - base) / mem->page_size;
}
```

### 21.3 mprotect según estado MSI

```c
dsm_status_t dsm_memory_protect(dsm_memory_t *mem, uint64_t page_id, dsm_msi_state_t state) {
    void *addr = dsm_memory_page_addr(mem, page_id);
    int prot;
    switch (state) {
        case DSM_PAGE_S: prot = PROT_READ; break;
        case DSM_PAGE_M: prot = PROT_READ | PROT_WRITE; break;
        case DSM_PAGE_I: default: prot = PROT_NONE; break;
    }
    if (mprotect(addr, mem->page_size, prot) != 0) return DSM_ERR_MPROTECT;
    return DSM_OK;
}
```

---

## 22. Detalle de implementación: coherencia

### 22.1 Inicialización del directorio

```c
dsm_status_t dsm_coherence_init(dsm_coherence_t *co, dsm_memory_t *mem,
                                dsm_stats_t *stats, const dsm_coherence_params_t *p,
                                const dsm_coherence_ops_t *ops) {
    co->mem = mem;
    co->stats = stats;
    co->node_id = p->node_id;
    co->home_node = p->home_node;
    co->n_nodes = p->n_nodes;
    co->timeout_ms = p->timeout_ms;
    // Inicializar directorio (hash table)
    co->dir = calloc(DSM_DIR_BUCKETS, sizeof(dsm_dir_entry_t *));
    dsm_mutex_init(&co->dir_lock, ...);
    dsm_mutex_init(&co->obj_lock, ...);
    // Inicializar objetos
    co->object_count = 0;
    co->next_page = mem->page_base_id;
    co->object_seq = 0;
    // Inicializar barrera
    atomic_init(&co->barrier_round, 0);
    atomic_init(&co->barrier_arrived, 0);
    co->initialized = 1;
    return DSM_OK;
}
```

### 22.2 Servicio de lectura del home

```c
static dsm_status_t home_serve_read(dsm_coherence_t *co, dsm_dir_entry_t *d,
                                    uint32_t requester, uint8_t *buf) {
    uint64_t page_id = d->meta.page_id;
    uint32_t owner = d->meta.owner_node;

    if (owner != co->home_node) {
        // Forward al dueño actual
        st = forward_from_owner(co, d, owner, 0, buf);
    } else {
        // Asegurar datos en el home
        st = home_ensure_data(co, page_id, buf);
        d->meta.owner_node = co->home_node;
        d->meta.state = DSM_PAGE_S;
        // Degradar copia del home a S
        local_install(co, page_id, NULL, DSM_PAGE_S);
    }
    // Agregar requester como sharer
    if (requester < 64) d->meta.sharers |= (1ULL << requester);
    d->meta.version++;
    dsm_mutex_unlock(&d->op_lock);
    return DSM_OK;
}
```

### 22.3 Servicio de escritura del home

```c
static dsm_status_t home_serve_write(dsm_coherence_t *co, dsm_dir_entry_t *d,
                                     uint32_t requester, uint8_t *buf) {
    uint64_t page_id = d->meta.page_id;
    uint32_t owner = d->meta.owner_node;

    if (owner != co->home_node) {
        st = forward_from_owner(co, d, owner, 1, buf);
    } else {
        st = home_ensure_data(co, page_id, buf);
        d->meta.owner_node = co->home_node;
    }
    // Invalidar a todos los sharers excepto el requester y el home
    others = d->meta.sharers & ~(1ULL << requester) & ~(1ULL << co->home_node);
    for (node = 0; node < 64; node++) {
        if (others & (1ULL << node)) {
            invalidate_node(co, page_id, node, 0, NULL, NULL);
            d->meta.sharers &= ~(1ULL << node);
        }
    }
    // Conceder propiedad al requester
    if (requester < 64) d->meta.sharers |= (1ULL << requester);
    d->meta.owner_node = requester;
    d->meta.state = DSM_PAGE_M;
    d->meta.version++;
    if (requester != co->home_node) {
        dsm_mutex_unlock(&d->op_lock);
        local_drop(co, page_id);  // home suelta su copia
    }
    return DSM_OK;
}
```

---

## 23. Detalle de implementación: red

### 23.1 Conexión persistente por peer

```c
// peer_acquire: toma el lock del peer y garantiza conexión
static int peer_acquire(dsm_net_t *net, uint32_t node, uint32_t timeout_ms) {
    dsm_peer_conn_t *c = &net->conn[node];
    dsm_mutex_lock(&c->lock);
    if (c->fd < 0) {
        // Conectar con timeout
        c->fd = connect_with_timeout(peer->host, peer->port, timeout_ms);
        if (c->fd < 0) {
            dsm_mutex_unlock(&c->lock);
            return -1;
        }
    }
    return c->fd;
}
```

### 23.2 RPC con reintentos

```c
static dsm_status_t rpc(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req,
                        dsm_msg_t *resp, uint32_t timeout_ms) {
    for (attempt = 0; attempt < max_attempts; attempt++) {
        st = rpc_once(net, dst, req, resp, timeout_ms);
        if (st == DSM_OK) return DSM_OK;
        if (st == DSM_ERR_CONFIG_MISMATCH) return st;  // no reintentar
        // backoff
        sleep(retry_backoff_ms * (attempt + 1));
    }
    return st;
}
```

---

## 24. Detalle de implementación: fallos de página

### 24.1 Manejador SIGSEGV

```c
static void dsm_segv_handler(int sig, siginfo_t *si, void *uctx) {
    dsm_thread_ctx_t *ctx = t_ctx;
    if (!installed || ctx == NULL || si == NULL) { reraise; return; }
    if (si->si_code != SEGV_ACCERR) { reraise; return; }
    if (ctx->in_fault || ctx->failed) { reraise; return; }

    // Calcular page_id desde la dirección del fallo
    page_id = ...;  // traducir a page_id global

    // Registrar en la cola circular
    queue_push(&req);

    // Esperar reanudación
    sigsuspend(waitmask);  // espera
}
```

### 24.2 Cola circular de fallos

```c
static int queue_push(const dsm_fault_req_t *req) {
    uint64_t head = atomic_fetch_add(&g_q.head, 1);  // reserva slot
    uint64_t slot = head & (g_q.cap - 1);
    g_q.slots[slot].req = *req;
    atomic_store_explicit(&g_q.slots[slot].ready, 1, memory_order_release);
    dsm_sem_post(&g_q.items);  // despierta al resolver
    return 1;
}
```

---

## 25. Detalle de implementación: heartbeat

### 25.1 Ciclo de heartbeat

```c
static void *heartbeat_loop(void *arg) {
    dsm_heartbeat_t *hb = arg;
    while (running) {
        for (i = 0; i < n_nodes; i++) {
            if (i == self) continue;
            st = dsm_net_ping(net, i, timeout_ms);
            if (st == DSM_OK) {
                hb->alive[i] = 1;
                hb->last_seen_ms[i] = dsm_now_ms();
            } else {
                hb->alive[i] = 0;
            }
        }
        sleep(interval_ms);
    }
}
```

### 25.2 Consulta de estado

```c
int dsm_heartbeat_alive(dsm_heartbeat_t *hb, uint32_t node) {
    return atomic_load_explicit(&hb->alive[node], memory_order_relaxed);
}
```

---

## 26. Detalle de implementación: barrera

### 26.1 Barrera centralizada

```c
dsm_status_t dsm_coherence_barrier(dsm_coherence_t *co, uint32_t timeout_ms) {
    uint32_t round = atomic_load(&co->barrier_round);
    // Enviar BARRIER al home con nuestro node_id y round
    // El home cuenta llegadas y publica la ronda cuando todos llegan
    // Sondear BARRIER_REPLY hasta que la ronda publicada == round+1
    // Timeout si no se completa
}
```

### 26.2 Llegada al home

```c
static uint32_t barrier_arrive(dsm_coherence_t *co, uint32_t node, uint32_t round) {
    dsm_mutex_lock(&co->barrier_lock);
    if (co->barrier_registered_round[node] == round) {
        // Ya registrado en esta ronda
        dsm_mutex_unlock(&co->barrier_lock);
        return atomic_load(&co->barrier_round);
    }
    co->barrier_registered_round[node] = round;
    co->barrier_arrived++;
    if (co->barrier_arrived >= co->n_nodes) {
        // Todos llegaron: publicar ronda
        atomic_store(&co->barrier_round, round + 1);
        co->barrier_arrived = 0;
    }
    dsm_mutex_unlock(&co->barrier_lock);
    return atomic_load(&co->barrier_round);
}
```

---

## 27. Detalle de implementación: expulsión

### 27.1 Selección de víctima

```c
uint64_t dsm_memory_pick_victim(dsm_memory_t *mem, uint32_t min_age_ms, uint64_t avoid_page) {
    // Recorrer todas las páginas residentes
    // Seleccionar la que tenga mayor edad (last_touch_ms más antiguo)
    // Evitar avoid_page
    // Retornar page_id de la víctima
}
```

### 27.2 Proceso de expulsión

```c
static dsm_status_t coherence_evict_one(dsm_coherence_t *co, uint64_t avoid_page) {
    victim = dsm_memory_pick_victim(co->mem, DSM_EVICT_MIN_AGE_MS, avoid_page);
    p = dsm_memory_page_lookup(co->mem, victim);
    // Lock de página
    state = p->meta.msi_state;
    dirty = p->meta.is_dirty;
    if (dirty) {
        buf = malloc(page_size);
        copy_out(victim, buf);  // capturar datos
    }
    // Marcar EVICTING, mprotect(NONE)
    p->meta.residency = DSM_RES_EVICTING;
    dsm_memory_protect(co->mem, victim, DSM_PAGE_I);
    unlock;
    // Home: swap_out (escribir a archivo)
    // Cliente: release_to_home (enviar datos al home)
    // Finalizar: marcar I, liberar marco, broadcast
}
```

---

## 28. Consideraciones de rendimiento

### 28.1 Cuellos de botella conocidos

1. **Pool de workers (red):** con `thread_pool_size=32` y 4 nodos, no es un cuello de botella. Con 16 nodos y muchas conexiones simultáneas, podría serlo.
2. **Directorio centralizado (home):** todas las operaciones de coherencia pasan por el home. Con muchos nodos, el home puede ser cuello de botella.
3. **Barrera centralizada:** el home cuenta llegadas. Con muchos nodos, el sondeo puede generar tráfico.
4. **Swap en el home:** escribir al archivo de swap es síncrono y bloquea el resolver.

### 28.2 Optimizaciones posibles

- **Prefetch:** traer páginas adyacentes cuando se accede a una.
- **Batch de invalidaciones:** agrupar invalidaciones para reducir RPCs.
- **Directorio distribuido:** particionar el directorio entre nodos (complejo).
- **Async swap:** escribir al swap en un hilo separado.

---

## 29. Consideraciones de seguridad

- **Sin autenticación:** el sistema es para laboratorio. No exponer puertos fuera de la red de pruebas.
- **Sin cifrado:** el tráfico TCP va en claro. No enviar datos sensibles.
- **Panel web:** solo lectura, pero expone estadísticas y tablas de páginas. Limitar acceso por firewall.
- **Huella de config:** detecta configuraciones incompatibles pero no previene ataques.
- **Fail-stop:** si un nodo cae, las operaciones fallan (no hay recuperación automática).
- **Datos sensibles:** no versionar IPs reales, carnés, nombres de profesor/institución. Se limpiará en la versión final.

---

## 30. Extras

### 30.1 Extra 1 — Ejecución distribuida (IMPLEMENTADO)

- **App:** `task` (`--app task --size N`).
- **Modelo:** coordinador (nodo 0) + trabajadores. Cada nodo procesa su porción en paralelo.
- **Resultados:** speedup, balance de carga, verificación contra referencia secuencial.
- **Métricas:** speedup > 1.0, balance cercano a 1.0.

### 30.2 Extra 2 — Tolerancia a fallos (NO IMPLEMENTADO)

- **Diseño documentado:** réplicas de páginas M, `REPLICA_WRITE`/`REPLICA_ACK`, promoción de réplica, checkpoints.
- **Estado:** diseño documentado, no implementado. `replication_factor` existe en config pero no se usa.
- **Motivo:** demasiado invasivo para el tiempo disponible; una implementación parcial sería peor que no tenerla.

---

## 31. Solución de problemas

| Síntoma | Causa | Solución |
|---|---|---|
| `bind: Address already in use` | Nodo anterior no terminó | `pkill -f dsm_node` |
| `config mismatch` | Huellas distintas | Comparar `dsmctl config` |
| `no se pudo conectar` | Nodo 0 no corriendo | Arrancar nodo 0 primero |
| `Timeouts` | Barrera inicial > 60 s | Arrancar con < 2 min de diferencia |
| `make` falla | Falta build-essential | `sudo apt install build-essential` |
| Chat no muestra nada | Barrera no pasó | Revisar logs |
| `verificacion FALLO` en evict | Pérdida de datos | Revisar swap |
| `abortado (archivo no disponible)` | Archivo no existe | Verificar ruta con `--file` |
| Zombies tras Ctrl+C | `dsm_fault_shutdown` no llamado | Verificar `dsm_runtime_destroy` |
| Huellas distintas | `page_size`/`node_memory`/etc. difieren | Comparar configs con `diff` |
| `SIGSEGV` sin resolver | Página fuera de la ventana | Verificar que el puntero esté en la ventana |
| `DSM_ERR_NOMEM` | Sin marcos disponibles | Aumentar `node_memory` o reducir páginas |
| `DSM_ERR_TIMEOUT` | Nodo caído o red lenta | Verificar heartbeat, aumentar timeout |

---

## 32. Checklist final

- [ ] Código compila sin warnings (`-Wall -Wextra -Wpedantic`).
- [ ] `dsm_node` y `dsmctl` generados.
- [ ] Configuración validada (misma huella en todos los nodos).
- [ ] Demo ejecuta sin errores.
- [ ] Matrices: `verificacion: OK`.
- [ ] File: `checksum completo = 0xc2dc9dc5, OK`.
- [ ] Counter: `suma = esperado, OK`.
- [ ] Bench: latencias impresas.
- [ ] Evict: `verificacion OK`, expulsiones > 0.
- [ ] Chat: mensajes cruzan.
- [ ] Task: `speedup > 1, balance ~1, OK`.
- [ ] Stress: OK.
- [ ] Fail-stop: nodo caído detectado.
- [ ] Config mismatch: detectado sin timeout.
- [ ] Ctrl+C: salida limpia.
- [ ] Panel web: responde.
- [ ] Informe: 21 secciones.
- [ ] Manual: completo.
- [ ] Git: commits de ambos integrantes.

---

*Fin de la guía de uso.* Con esto se puede configurar, operar y verificar el sistema completo en 1, 2, 3 o más PCs. Para pruebas detalladas con capturas de pantalla, ver `LAB_PRUEBAS.md`. Para el informe técnico, ver `Entrega_Final/04_Entrega_Final.md`. Para el manual de usuario, ver `Entrega_Final/secciones/18_manual_usuario.md`.