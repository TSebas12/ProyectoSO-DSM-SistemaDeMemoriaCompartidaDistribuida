# Guía de Uso Completa — DSM (Entrega Final)

> Documento profesional y exhaustivo para configurar, operar, comprender y verificar el Sistema de Memoria Compartida Distribuida (DSM). Incluye arquitectura interna, módulos de código, configuración detallada, pruebas y solución de problemas.

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

---

## 2. Arquitectura del sistema

### 2.1 Visión general

El sistema se compone de N nodos. El nodo 0 es el **home**: mantiene el directorio de páginas (quién es dueño de cada página, en qué estado está, quiénes la comparten). Los demás nodos son **clientes** que solicitan páginas al home.

```
┌─────────────────────────────────────────────────────────┐
│                    ESPACIO COMPARTIDO                    │
│              (ventana mmap, 256 MB default)              │
├──────────┬──────────┬──────────┬──────────┬─────────────┤
│  Nodo 0  │  Nodo 1  │  Nodo 2  │  Nodo 3  │  ...        │
│  (home)  │ (client) │ (client) │ (client) │             │
│          │          │          │          │             │
│ Dir. +   │ Caché    │ Caché    │ Caché    │             │
│ Coher.   │ M/S/I    │ M/S/I    │ M/S/I    │             │
└──────────┴──────────┴──────────┴──────────┴─────────────┘
```

### 2.2 Componentes por nodo (archivos de código)

| # | Componente | Archivo(s) | Función principal |
|---|---|---|---|
| 1 | Memoria virtual | `memory.c`, `memory.h` | `mmap` de ventana (PROT_NONE), tabla de páginas hash, pool de marcos, swap |
| 2 | Coherencia MSI | `coherence.c`, `coherence.h` | Directorio, transiciones M/S/I, invalidación, fetch, expulsión, barrera |
| 3 | Red | `net.c`, `net.h` | Sockets TCP, pool de workers, RPC, huella de config |
| 4 | Protocolo | `protocol.c`, `protocol.h` | Cabecera 64 B, FNV-1a, serialización |
| 5 | Fallos de página | `fault.c`, `fault.h` | Manejador SIGSEGV, cola de fallos, reanudación |
| 6 | Runtime | `runtime.c`, `runtime.h` | Inicialización, servicios, ciclo de vida, resolver |
| 7 | API pública | `dsm_api.c`, `dsm_api.h` | `dsm_malloc`, `dsm_barrier`, `dsm_release`, etc. |
| 8 | Configuración | `config.c`, `config.h` | Parseo, validación, huella |
| 9 | Nodos | `nodes.c`, `nodes.h` | Parseo de tabla de nodos |
| 10 | Control | `control.c`, `control.h` | Servidor TCP para dsmctl |
| 11 | Web | `web.c`, `web.h` | Panel HTTP solo lectura |
| 12 | Heartbeat | `heartbeat.c`, `heartbeat.h` | PING/PONG, detección de nodos |
| 13 | Sync | `sync.c`, `sync.h` | Mutex/cond/semáforos con verificación de orden |
| 14 | Stats | `stats.c`, `stats.h` | Contadores de rendimiento |
| 15 | Log | `log.c`, `log.h` | Logging a archivo |
| 16 | Tipos | `dsm_types.h` | Enums, structs, constantes |
| 17 | Apps | `apps.c`, `app_*.c` | Aplicaciones de prueba |
| 18 | Demo | `demo_page.c` | Demostración de página |
| 19 | Main | `node_main.c` | Binario del nodo |
| 20 | ctl_main | `ctl_main.c` | Cliente de control |

### 2.3 Modelo de memoria (detalle)

```
┌──────────────────────────────────────────────────────┐
│              VENTANA (mmap, PROT_NONE)               │
│              256 MB (window_size)                    │
│  ┌────┐ ┌────┐ ┌────┐     ┌────┐                   │
│  │ P0 │ │ P1 │ │ P2 │ ... │ PN │  (page_size=4096) │
│  └────┘ └────┘ └────┘     └────┘                   │
├──────────────────────────────────────────────────────┤
│              BACKING (memfd_create)                  │
│              256 MB (siempre escribible)              │
│  ┌────┐ ┌────┐ ┌────┐     ┌────┐                   │
│  │ P0 │ │ P1 │ │ P2 │ ... │ PN │                   │
│  └────┘ └────┘ └────┘     └────┘                   │
├──────────────────────────────────────────────────────┤
│              TABLA DE PÁGINAS (hash)                 │
│  page_id → {msi_state, residency, frame_slot,       │
│             is_dirty, version, lock, cond}           │
├──────────────────────────────────────────────────────┤
│              POOL DE MARCOS (node_memory/page_size)  │
│              256 marcos (default 1 MB)               │
└──────────────────────────────────────────────────────┘
```

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
    - Si FETCHING/EVICTING → cond_wait
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
- **Lectura (PAGE_REQUEST):** si owner != home → forward al owner. Si owner == home → ensure_data, state=S, sharers |= requester.
- **Escritura (OWNERSHIP_REQUEST):** si owner != home → forward. Si owner == home → ensure_data, invalidar sharers, conceder M al requester, local_drop (home suelta su copia).
- **Invalidación (INVALIDATE):** el nodo descarta su copia, responde ACK.
- **Release (PAGE_RELEASE):** un nodo suelta su copia M, envía datos al home. El home instala los datos, invalida sharers, se queda como owner con S.

**Coherencia del home:** cuando el home sirve una lectura, su propia copia se degrada a S (`local_install(NULL, S)`) para que la próxima escritura pase por el protocolo e invalide a los demás.

### 2.6 Red y protocolo

**Cabecera (64 bytes):**
```
offset  size  campo
0       8     request_id
8       8     page_id
16      4     magic (DSM_MAGIC)
20      2     version (DSM_PROTOCOL_VERSION)
22      2     type (DSM_MSG_*)
24      4     flags
28      4     header_len (64)
32      4     payload_len
36      4     src_node
40      4     dst_node
44      4     status
48      4     checksum (FNV-1a)
52      4     payload_off (64)
56      2     ttl
58      2     reserved
60      4     reserved2 (huella de config)
```

**Tipos de mensaje:** PING, PONG, PAGE_REQUEST, PAGE_DATA, OWNERSHIP_REQUEST, OWNERSHIP_GRANT, PAGE_FORWARD, INVALIDATE, INVALIDATE_ACK, PAGE_WRITEBACK, PAGE_WRITEBACK_ACK, PAGE_RELEASE, ACK, OBJECT_ALLOC, OBJECT_ALLOC_REPLY, BARRIER, BARRIER_REPLY, PAGE_LIST, PAGE_LIST_REPLY, STATS_REQUEST, STATS_REPLY, ERROR.

**Huella de configuración:** FNV-1a de `config_version, page_size, node_memory, window_size, max_nodes, port_range_*, replication_factor, max_message_size, heartbeat_*, network_timeout, connect_timeout, thread_pool_size, barrier_timeout, DSM_PROTOCOL_VERSION` + tabla de nodos (ordenada por id). Si dos nodos tienen huellas distintas, `DSM_ERR_CONFIG_MISMATCH`.

**Pool de workers:** `thread_pool_size` hilos atienden conexiones entrantes persistentes. Cada conexión se atiende en serie por un worker (no hay un hilo por conexión).

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

**Descripción de cada clave:**

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

### 3.2 Tabla de nodos `config/nodes_N.conf`

Formato: `id host puerto` (una línea por nodo).

```
0 127.0.0.1 27300
1 127.0.0.1 27304
2 127.0.0.1 27308
3 127.0.0.1 27312
```

Puertos: `27300 + 4*node_id`. Rango: 27300–27399 (pareja #3).

### 3.3 Argumentos CLI de `dsm_node`

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

### 3.4 `dsmctl` (cliente de control)

```bash
./dsmctl --config config/node0.conf stats    # estadísticas
./dsmctl --config config/node0.conf pages    # páginas locales
./dsmctl --config config/node0.conf dir      # directorio (solo home)
./dsmctl --config config/node0.conf nodes    # nodos + heartbeat
./dsmctl --config config/node0.conf config   # huella de configuración
./dsmctl --host IP --port PUERTO stats       # conexión remota
./dsmctl --timeout MS ...                    # timeout personalizado
```

### 3.5 Panel web

Si `web_enabled=1`:
- `http://HOST:WEB_PORT/` → HTML con estado del nodo.
- `http://HOST:WEB_PORT/stats.json` → JSON con estadísticas.

### 3.6 `dsm.sh` — menú interactivo

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

*Fin de la guía de uso.* Con esto se puede configurar, operar y verificar el sistema completo en 1, 2, 3 o más PCs. Para pruebas detalladas con capturas de pantalla, ver `LAB_PRUEBAS.md`.