# Guía de Uso Completa — DSM (Entrega Final)

> Documento profesional para configurar, operar y comprender el sistema DSM. Incluye arquitectura, módulos de código, configuración, pruebas y solución de problemas.

---

## 1. Introducción

El Sistema de Memoria Compartida Distribuida (DSM) permite que múltiples procesos en máquinas separadas compartan un espacio de memoria virtual unificado. La coherencia se mantiene mediante el protocolo MSI (Modified, Shared, Invalid) con paginación bajo demanda.

**Características principales:**
- Memoria compartida transparente vía `mmap` + `SIGSEGV`.
- Coherencia MSI con invalidación.
- Paginación bajo demanda (páginas se traen solo cuando se acceden).
- Expulsión LRU con swap en el home.
- Heartbeat para detección de nodos caídos (fail-stop).
- Panel web de solo lectura.
- CLI de control (`dsmctl`).
- 8 aplicaciones de prueba.

---

## 2. Arquitectura del sistema

### 2.1 Componentes por nodo

| Componente | Archivo | Función |
|---|---|---|
| Memoria virtual | `memory.c` | `mmap` de ventana (PROT_NONE), tabla de páginas, marcos, swap |
| Coherencia MSI | `coherence.c` | Directorio, transiciones M/S/I, invalidación, fetch, expulsión |
| Red | `net.c` | Sockets TCP, pool de workers, RPC, huella de config |
| Protocolo | `protocol.c` | Cabecera de 64 B, FNV-1a checksum, serialización |
| Fallos de página | `fault.c` | Manejador SIGSEGV, cola de fallos, reanudación |
| Runtime | `runtime.c` | Inicialización, servicios, ciclo de vida |
| API pública | `dsm_api.c` | `dsm_malloc`, `dsm_barrier`, `dsm_release`, etc. |
| Configuración | `config.c` | Parseo de `nodeN.conf`, validación, huella |
| Nodos | `nodes.c` | Parseo de tabla de nodos |
| Control | `control.c` | Servidor TCP para `dsmctl` |
| Web | `web.c` | Panel HTTP de solo lectura |
| Heartbeat | `heartbeat.c` | PING/PONG, detección de nodos vivos |
| Sync | `sync.c` | Mutex/cond/semáforos con verificación de orden |
| Stats | `stats.c` | Contadores de rendimiento |

### 2.2 Modelo de memoria

- **Ventana:** `mmap(NULL, window_size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)` → toda la ventana sin permisos.
- **Backing:** `memfd_create` con tamaño `window_size` → páginas físicas siempre disponibles.
- **Tabla de páginas:** hash table (`dsm_page_entry_t`) con metadatos MSI, marco, residencia.
- **Marcos:** pool de `node_memory / page_size` marcos (default 256 para 1 MB).
- **Swap:** archivo `logs/swap-nodeN.bin` (solo el home escribe; los clientes liberan al home).

### 2.3 Flujo de una operación de memoria

1. App accede a `*(ptr)` en la ventana → SIGSEGV (PROT_NONE).
2. Manejador `dsm_segv_handler` registra el fallo en la cola (`queue_push`).
3. Hilo resolver (`resolver_loop`) toma el fallo (`queue_pop`).
4. `dsm_coherence_handle_fault`:
   - Si la página está en FETCHING/EVICTING → esperar (cond_wait).
   - Si es RESIDENT y el estado MSI permite el acceso → hit (retornar OK).
   - Si no → FETCHING, fetch (local o remoto), instalar página, publicar permisos (mprotect), RESIDENT.
5. `dsm_fault_wake` → `pthread_kill(SIGUSR1)` → app se reanuda.

### 2.4 Coherencia MSI

| Estado | Permiso | Significado | Transiciones |
|---|---|---|---|
| M (Modified) | RW | Único dueño, puede escribir | M→I (invalidación), M→S (democión en lectura) |
| S (Shared) | R | Copia de lectura, puede haber sharers | S→M (solicitud de escritura), S→I (invalidación) |
| I (Invalid) | Ninguno | No residente | I→M (escritura), I→S (lectura) |

**Home:** mantiene el directorio (`dsm_dir_entry_t`) con owner, state, sharers, version.
**Clientes:** solicitan al home (PAGE_REQUEST, OWNERSHIP_REQUEST).

### 2.5 Red y protocolo

- **Cabecera:** 64 bytes exactos (magic, version, type, flags, page_id, src/dst, status, checksum, reserved2=huella).
- **Tipos de mensaje:** PAGE_REQUEST, OWNERSHIP_REQUEST, PAGE_FORWARD, INVALIDATE, PAGE_WRITEBACK, PAGE_RELEASE, OBJECT_ALLOC, BARRIER, PING, STATS.
- **Huella de config:** FNV-1a de parámetros + tabla de nodos en `reserved2`. Si difiere, `DSM_ERR_CONFIG_MISMATCH`.
- **Pool de workers:** `thread_pool_size` hilos atienden conexiones entrantes persistentes.

---

## 3. Configuración completa

### 3.1 Archivo `config/nodeN.conf`

Todas las claves disponibles (ver sección 1.1 de la guía de pruebas). Valores por defecto en `config/node0.conf`:

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

| Argumento | Descripción |
|---|---|
| `--config RUTA` | Archivo de config del nodo |
| `--nodes RUTA` | Tabla de nodos |
| `--app NOMBRE` | demo, matrices, file, counter, bench, evict, chat, task, stress |
| `--size N` | Tamaño de matriz o bytes de archivo |
| `--pages N` | Páginas para bench/evict |
| `--iters N` | Iteraciones |
| `--threads N` | Hilos por nodo (counter) |
| `--file RUTA` | Archivo para app file |
| `--nick NOMBRE` | Nick para chat |
| `--text TEXTO` | Texto inicial para chat |
| `--duration S` | Duración en segundos (chat) |
| `--frames N` | Marcos por nodo (evict) |
| `--no-verify` | Omitir verificación |
| `--serve` | Quedar escuchando después de la app |
| `--check-config` | Validar config y salir (imprime huella) |
| `--version` | Versión |

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
- `dsm_memory_protect`: `mprotect` según estado MSI.
- `dsm_memory_copy_in/out`: copia datos entre ventana y buffer (vía backing).
- `dsm_memory_swap_out/in`: escribe/lee el archivo de swap.
- `dsm_memory_pick_victim`: selecciona víctima LRU para expulsión.

### 4.2 `coherence.c` — Coherencia MSI

- `dsm_coherence_init`: inicializa directorio, objetos, barrera.
- `dsm_coherence_handle_fault`: resuelve fallo de página (fetch + instalar + publicar permisos).
- `home_serve_read`: sirve lectura (forward si no es home, o ensure_data + democión).
- `home_serve_write`: sirve escritura (forward si no es home, o ensure_data + invalidar sharers + conceder M).
- `handle_invalidate`: descarta copia local y confirma.
- `handle_page_release`: recibe página liberada, instala datos, actualiza directorio.
- `dsm_coherence_alloc`: reserva objeto (monótono, determinista por seq).
- `dsm_coherence_barrier`: barrera centralizada por sondeo.
- `dsm_coherence_release_page`: publica y suelta una página (Extra 1 / chat).

### 4.3 `fault.c` — Manejador de SIGSEGV

- `dsm_segv_handler`: registra el fallo en la cola (productor).
- `dsm_fault_queue_pop`: consume fallos (consumidor único).
- `dsm_fault_wake`: reanuda el hilo (`SIGUSR1`).
- `dsm_fault_mark_failed`: marca el hilo como fallido (fail-stop).
- Cola circular con semáforo (`dsm_sem_t items`).

### 4.4 `net.c` — Red

- `dsm_net_create/start/stop/destroy`: ciclo de vida.
- `peer_acquire/release`: conexión persistente por peer con lock.
- `rpc/rpc_once`: envío y recepción de RPC.
- `serve_connection`: atiende conexión persistente (worker).
- `listener_loop`: acepta conexiones, encola en `queue`.
- `dsm_net_ping`: ping con timeout.
- Huella de config en `reserved2` (offset 60).

### 4.5 `protocol.c` — Protocolo

- `dsm_header_encode/decode`: serialización de 64 B.
- `dsm_protocol_encode/decode`: mensaje completo (cabecera + payload).
- `dsm_checksum_fnv1a`: FNV-1a de la carga.

### 4.6 `config.c` — Configuración

- `dsm_config_load`: parsea `nodeN.conf`.
- `dsm_config_validate`: valida rangos y coherencia.
- `dsm_config_fingerprint`: FNV-1a de parámetros + tabla de nodos.

### 4.7 `nodes.c` — Tabla de nodos

- `dsm_nodes_load`: parsea `nodes_N.conf`.
- `dsm_nodes_print`: imprime la tabla.

### 4.8 `runtime.c` — Runtime

- `dsm_runtime_init`: inicializa memoria, fault, coherencia, red, resolver.
- `dsm_runtime_start_services`: control, web, heartbeat.
- `dsm_runtime_destroy`: apagado ordenado.
- `resolver_loop`: hilo que resuelve fallos de página (con reintento).

### 4.9 `control.c` / `web.c` — Servicios

- Control: TCP, recibe comando de texto, responde texto. Sin autenticación (laboratorio).
- Web: HTTP GET `/` (HTML) y `/stats.json` (JSON). Solo lectura.

### 4.10 `heartbeat.c` — Detección de nodos

- Hilo que recorre peers, envía PING, registra alive/last_seen.
- `dsm_heartbeat_alive`: consulta estado.
- `dsm_heartbeat_format`: formatea para `dsmctl nodes`.

---

## 5. Aplicaciones de prueba

| App | Archivo | Descripción | Parámetros |
|---|---|---|---|
| demo | `demo_page.c` | Demostración de una página compartida | — |
| matrices | `app_matrices.c` | Multiplicación por bloques, verifica C=A×B | `--size N` |
| file | `app_file.c` | Checksum distribuido de archivo | `--file RUTA` |
| counter | `app_counter.c` | Incrementos concurrentes | `--iters N --threads N` |
| bench | `app_bench.c` | Latencia local/remota/escritura | `--iters N` |
| evict | `app_evict.c` | Presión de memoria, swap | `--pages N --frames M` |
| chat | `app_chat.c` | Texto libre entre nodos | `--nick --text --duration` |
| task | `app_task.c` | Ejecución distribuida (Extra 1) | `--size N` |
| stress | `apps.c` | matrices 192 + counter 100×2 | — |

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
| Zombies tras Ctrl+C | `dsm_fault_shutdown` no llamado | Verificar `dsm_runtime_destroy` |

---

## 10. Checklist final

- [ ] Código compila sin warnings.
- [ ] `dsm_node` y `dsmctl` generados.
- [ ] Configuración validada (misma huella).
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