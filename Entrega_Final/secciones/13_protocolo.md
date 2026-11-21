# 13. Protocolo de mensajería

## 13.1 Cabecera

Cabecera fija de **64 bytes** (`dsm_msg_header_t`), serializada con FNV-1a de los datos:

| Campo | Tipo | Bytes | Descripción |
|---|---|---|---|
| `request_id` | uint64 | 8 | Identificador único de la RPC |
| `page_id` | uint64 | 8 | Página involucrada |
| `magic` | uint32 | 4 | `0x44534D32` (DSM2) |
| `version` | uint16 | 2 | Versión del protocolo (2) |
| `type` | uint16 | 2 | Tipo de mensaje |
| `flags` | uint32 | 4 | Banderas |
| `header_len` | uint32 | 4 | 64 |
| `payload_len` | uint32 | 4 | Longitud del payload |
| `src_node` | uint32 | 4 | Nodo origen |
| `dst_node` | uint32 | 4 | Nodo destino |
| `status` | uint32 | 4 | Estado |
| `checksum` | uint32 | 4 | FNV-1a del payload |
| `payload_off` | uint32 | 4 | Offset del payload |
| `ttl` | uint16 | 2 | Time-to-live |
| `reserved` | uint16 | 2 | Reservado |
| `reserved2` | uint32 | 4 | **Huella de configuración** (FNV-1a de parámetros + tabla de nodos) |

## 13.2 Mensajes mínimos

| Mensaje | Propósito |
|---|---|
| `PAGE_REQUEST` | Solicitar una página al home |
| `PAGE_DATA` | Transferir una página |
| `INVALIDATE` | Invalidar una copia en un nodo |
| `OWNERSHIP_GRANT` | Conceder propiedad (M) a un nodo |
| `PAGE_RELEASE` | Devolver una página al home |
| `BARRIER` | Sincronización de nodos |
| `PING` / `PONG` | Heartbeat |
| `STATS` | Consulta de estadísticas |
| `OBJECT_ALLOC` | Asignación de objeto compartido |

## 13.3 Mensajes adicionales

| Mensaje | Uso |
|---|---|
| `PAGE_FORWARD` | El home pide la página al dueño actual |
| `INVALIDATE_ACK` | Confirmación de invalidación |
| `BARRIER_ACK` | Confirmación de barrera |
| `NODE_INFO` / `NODE_LIST` | Información y listado de nodos |
| `OBJECT_FREE` | Liberación de objeto |
| `ERROR` | Error genérico |
| `HELLO` / `HELLO_ACK` | (Reservado) |

## 13.4 Payloads

Los payloads estructurados (`dsm_alloc_req_t`, `dsm_barrier_req_t`, `dsm_stats_wire_t`, `dsm_page_info_t`) se envían tal cual, sin huecos, para poder deserializarlos directamente. Ver `dsm_protocol.h`.

## 13.5 Flujos principales

1. **Fallo de página (cliente):** SIGSEGV → cola → resolver → `PAGE_REQUEST` al home → `PAGE_DATA` → instalación local → reanudación.
2. **Escritura remota:** `PAGE_WRITE` al home → invalidación de sharers → `OWNERSHIP_GRANT` → `local_drop` en el home.
3. **Barrera:** cada nodo envía `BARRIER` al home; el home responde cuando todos llegaron.
4. **Asignación:** `OBJECT_ALLOC` con `seq`; el home devuelve el mismo `page_base` a todos.

## 13.6 Timeouts, reintentos e idempotencia

- Timeout de RPC: 10 s. Timeout de conexión: 5 s.
- Reintentos: 5 con backoff de 500 ms.
- `request_id` para detectar duplicados.
- Mensaje truncado: se descarta y se cierra la conexión.
- Versión del protocolo: 2. Mensajes con versión distinta se rechazan.

## 13.7 Huella de configuración

El campo `reserved2` (4 bytes) lleva una huella FNV-1a de la configuración del clúster (parámetros + tabla de nodos). Si dos nodos tienen huellas distintas, la operación falla con `DSM_ERR_CONFIG_MISMATCH` en lugar de esperar un timeout.

## 13.8 Errores de protocolo

| Código | Significado |
|---|---|
| `DSM_ERR_PEER_DOWN` | Nodo caído o inalcanzable |
| `DSM_ERR_TIMEOUT` | Timeout de RPC |
| `DSM_ERR_NET` | Error de red |
| `DSM_ERR_PROTOCOL` | Magic o versión incorrectos |
| `DSM_ERR_CHECKSUM` | Checksum del payload no coincide |
| `DSM_ERR_CONFIG_MISMATCH` | Configuración incompatible entre nodos |

Cada error tiene código, mensaje legible, nodo y `request_id` para trazabilidad.

---

*Fin de la sección 13.*
