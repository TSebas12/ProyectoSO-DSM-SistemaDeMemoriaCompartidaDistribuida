# 11. Estructuras de datos y archivos

## 11.1 Tablas principales

| Estructura | Archivo | Responsabilidad | Tamaño |
|---|---|---|---|
| `dsm_config_t` | `dsm_config.h` | Configuración del nodo | 704 B |
| `dsm_nodes_t` / `dsm_peer_t` | `dsm_nodes.h` | Tabla de peers (id, host, port) | O(1) por id |
| `dsm_page_entry_t` | `dsm_memory.h` | Entrada de página local (MSI, residencia, frame, versión) | — |
| `dsm_dir_entry_t` | `dsm_coherence.h` | Entrada de directorio (owner, sharers, estado, op_lock) | — |
| `dsm_msg_header_t` | `dsm_protocol.h` | Cabecera de 64 B (magic, version, type, flags, src/dst, checksum, reserved2) | 64 B exactos |
| `dsm_alloc_req_t` / `dsm_alloc_reply_t` | `dsm_protocol.h` | Asignación de objetos | tamaño fijo |
| `dsm_barrier_req_t` / `dsm_barrier_reply_t` | `dsm_protocol.h` | Barrera centralizada | tamaño fijo |
| `dsm_stats_wire_t` | `dsm_protocol.h` | Estadísticas por nodo | `field_count` + array |
| `dsm_page_info_t` | `dsm_protocol.h` | Info de página para `dsmctl pages` | tamaño fijo |

## 11.2 Directorio de coherencia

- Hash encadenado de 256 buckets.
- Un `op_lock` por entrada (clase GLOBAL).
- Campos: `page_id`, `object_id`, `version`, `owner_node`, `sharers`, `state`, `last_update_ms`.

## 11.3 Configuración

- Archivos: `node0.conf` … `node3.conf` (uno por nodo) + `nodes.conf` (4 nodos) o `nodes_2.conf` (2 nodos).
- Parámetros clave: `page_size` (4096), `node_memory` (1 048 576), `window_size` (268 435 456), `max_nodes` (16), `thread_pool_size` (32), `retry_count` (5), `replication_factor` (0).
- Validación: rangos, puertos únicos dentro del nodo, ventana múltiplo de página, heartbeat timeout ≥ 2 × intervalo.
- `--check-config` valida y sale sin arrancar.

## 11.4 Archivos y directorios

| Ruta | Contenido |
|---|---|
| `config/nodeN.conf` | Configuración del nodo N |
| `config/nodes.conf` | Tabla de 4 nodos |
| `config/nodes_2.conf` | Tabla de 2 nodos |
| `logs/dsm-nodeN.log` | Log por nodo |
| `logs/swap-nodeN.bin` | Swap del home (archivo disperso 256 MB) |
| `Entrega_Final/evidence/` | Logs y salidas de pruebas (versionados) |
| `Entrega_Final/secciones/` | Secciones del informe (una por archivo) |

## 11.5 Logs

- Niveles: ERROR, WARN, INFO, DEBUG, TRACE.
- Cada línea con timestamp y nivel; `fflush` por línea.
- Rotación manual (no automática en esta versión).

---

*Fin de la sección 11.*
