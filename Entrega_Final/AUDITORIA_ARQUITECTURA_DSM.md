# Auditoría de Arquitectura DSM — Entrega_Final/codigo/

**Fecha:** 2026-11-27  
**Agente:** AGENTE 2 — Arquitectura DSM, nodos y escalabilidad  
**Alcance:** Auditoría completa de la arquitectura distribuida (solo lectura, sin modificaciones)

---

## 1. LÍMITES ACTUALES (Arrays fijos / Constantes hardcodeadas)

| Constante | Valor | Ubicación | Comentario |
|-----------|-------|-----------|------------|
| `DSM_MAX_NODES` | **16** | `include/dsm_types.h:27` | Límite absoluto de nodos en el clúster |
| `DSM_MAX_PEERS` | **16** (`= DSM_MAX_NODES`) | `include/dsm_types.h:28` | Alias, usado en `dsm_nodes_t.peer[DSM_MAX_PEERS]` |
| `DSM_MAX_OBJECTS` | **128** | `include/dsm_coherence.h:79` | Límite de objetos compartidos (`dsm_coherence.objects[128]`) |
| `DSM_NET_MAX_WORKERS` | **32** | `src/net.c:43` | Hilos de trabajo del pool de red |
| `DSM_NET_QUEUE_LEN` | **64** | `src/net.c:44` | Cola de sockets aceptados |
| `DSM_NET_MAX_ACTIVE` | **64** | `src/net.c:45` | Conexiones activas simultáneas |
| `DSM_DIR_BUCKETS_DEFAULT` | **256** | `src/coherence.c:43` | Cubetas del directorio hash (configurable via `dir_buckets`) |
| `DSM_PAGE_SIZE_DEFAULT` | **4096** | `include/dsm_types.h:24` | Tamaño de página (potencia de 2, 4KB–64KB) |
| `DSM_MAX_OBJECTS` (alloc_cache) | **128** | `include/dsm_coherence.h:129` | Caché determinista de asignaciones |
| `barrier_registered_round` | **16** | `include/dsm_coherence.h:139` | Array fijo `[DSM_MAX_NODES]` |

**Evidencia de arrays fijos:**
- `dsm_nodes_t.peer[DSM_MAX_PEERS]` — `include/dsm_nodes.h:38`
- `dsm_coherence.objects[DSM_MAX_OBJECTS]` — `include/dsm_coherence.h:113`
- `alloc_cache[DSM_MAX_OBJECTS]` — `include/dsm_coherence.h:129`
- `barrier_registered_round[DSM_MAX_NODES]` — `include/dsm_coherence.h:139`
- `conn[DSM_MAX_PEERS]` en `dsm_net_t` — `src/net.c:61`

---

## 2. TOPOLÓGÍAS SOPORTADAS

El sistema soporta **tres modalidades de despliegue**, documentadas en `include/dsm_nodes.h:8-11` y configurables vía `dsm.sh` / `lab.sh`:

| Topología | Descripción | Archivo nodes | Nodos | PCs |
|-----------|-------------|---------------|-------|-----|
| **1 PC / N nodos** | Todos los nodos en localhost (127.0.0.1) | `nodes_1.conf` (1), `nodes_2.conf` (2), `nodes_4.conf` (4) | 1–4 | 1 |
| **N PCs / 1 nodo** | 1 nodo por PC, IPs reales en `nodes.conf` | `nodes.conf` (generado por `dsm.sh`) | 2–4 | 2–4 |
| **N PCs / M nodos** | Múltiples nodos por PC (distinto `listen_port` cada uno) | `nodes.conf` con IPs + puertos explícitos | Hasta 16 | 1–4 |

**Evidencia:**
- `include/dsm_nodes.h:8-11`: *"En el prototipo 1 la dirección de cada nodo se deducía del puerto base y el destino remoto se forzaba a 127.0.0.1, lo que impedía correr más de un nodo por máquina. El prototipo 2 lee un archivo de nodos con la IP real de cada uno; así el mismo binario sirve para 2 o 4 máquinas y para pruebas locales."*
- `dsm.sh:232-240` función `nodes_file_for_topology()` → `config/nodes_${TOPOLOGY}.conf`
- `dsm.sh:275-290` `write_nodes_file()` genera `nodes_N.conf` con IPs y puertos base fijos (27300, 27304, 27308, 27312)
- `lab.sh:106, 110, 113, 116, 119, 124` usa `NODES_FILE=config/nodes_2.conf` o `nodes_4.conf` según test

---

## 3. IDENTIFICADORES DE NODO: Rango, Asignación, Conflictos

| Aspecto | Detalle | Evidencia |
|---------|---------|-----------|
| **Rango válido** | `0` a `DSM_MAX_NODES - 1` = **0–15** | `include/dsm_types.h:27`, `src/nodes.c:107` (`id >= DSM_MAX_NODES` → error) |
| **Asignación** | Manual vía `node_id` en cada `node<N>.conf` | `config/node0.conf:5` (`node_id = 0`), `node1.conf:3` (`node_id = 1`), etc. |
| **Validación** | `dsm_config_validate()` rechaza `node_id >= max_nodes` | `src/config.c:345-347` |
| **Unicidad en tabla nodos** | `dsm_nodes_load()` rechaza duplicados (`DSM_ERR_EXIST`) | `src/nodes.c:111-114` |
| **Auto-asignación** | **No existe** — cada nodo debe tener `node_id` único en su config | `node_main.c:109-110` default `config/node0.conf` |
| **Conflictos** | Si dos nodos usan mismo `node_id` en la tabla → `DSM_ERR_EXIST` al cargar | `src/nodes.c:111-114` |
| **Seed node** | Nodo 0 (home) en puerto 27300 — hardcodeado en `seed_host/seed_port` | `config/node*.conf:13-14`, `config.c:136-137` defaults |

**Problema detectado:** No hay mecanismo automático de asignación de `node_id` ni detección de colisiones entre archivos de config independientes. Cada operador debe coordinar manualmente.

---

## 4. HOME: Selección, Configuración, Responsabilidades, Fallos

### 4.1 Selección del Home
- **Hardcodeado a nodo 0** en `runtime.c:161`: `cp.home_node = 0;`
- No configurable por archivo de config ni descubrimiento dinámico.

### 4.2 Configuración
- `dsm_coherence_params_t.home_node` — `include/dsm_coherence.h:93`
- Inicializado en `dsm_coherence_init()` línea 270: `co->home_node = (p != NULL) ? p->home_node : 0u;`

### 4.3 Responsabilidades (evidencia en `coherence.c`)
| Responsabilidad | Código |
|----------------|--------|
| **Directorio global** | `dir` hash table en `dsm_coherence_t` (solo en home) |
| **Asignación de objetos** | `dsm_coherence_alloc()` — home asigna `page_base` y `object_id` (línea 1720-1748) |
| **Servicio de PAGE_REQUEST** | `handle_page_request()` línea 1391 — solo home (`co->node_id != co->home_node` → `DSM_ERR_NOTSUP`) |
| **Servicio de OWNERSHIP_REQUEST** | `handle_ownership_request()` línea 1423 — solo home |
| **Invalidaciones** | `home_serve_write()` línea 1008-1091 invalida a `sharers` |
| **Writeback / Release** | `release_to_home()` línea 627 — clientes sucios envían datos al home |
| **Swap** | Home **nunca pierde datos**: escribe a swap en `coherence_evict_one()` línea 721-735 |
| **Barrera centralizada** | `dsm_coherence_barrier()` — home coordina rondas |

### 4.4 Fallos del Home
- **Sin failover**: Si nodo 0 cae, el clúster **deja de funcionar** (no hay elección de nuevo home).
- **Fail-stop**: Operaciones que requieren home (`PAGE_REQUEST`, `OWNERSHIP_REQUEST`, `OBJECT_ALLOC`, `BARRIER`) fallan con timeout/error.
- **Heartbeat** detecta caída (`heartbeat.c:84-88`), pero no hay reacción automática (reconfiguración, migración de directorio).
- **Evidencia**: `heartbeat.c:84-88` declara nodo caído tras `heartbeat_missed_max` pings fallados, pero `coherence.c` no reacciona a `alive[i] == 0`.

---

## 5. DISTRIBUCIÓN DE NODOS ENTRE PCs: Configuración y Límites

### 5.1 Esquema de Puertos (Pareja #3)
| Nodo | listen_port | control_port | heartbeat_port | web_port | Rango |
|------|-------------|--------------|----------------|----------|-------|
| 0 (Home) | 27300 | 27301 | 27302 | 27303 | 27300–27303 |
| 1 | 27304 | 27305 | 27306 | 27307 | 27304–27307 |
| 2 | 27308 | 27309 | 27310 | 27311 | 27308–27311 |
| 3 | 27312 | 27313 | 27314 | 27315 | 27312–27315 |
| N | 27300+4N | +1 | +2 | +3 | Fórmula: `base_port + 4 * node_id` |

**Evidencia:**
- `include/dsm_nodes.h:18-19`: *"El puerto es opcional: si se omite se usa base_port + 4 * node_id, que es la regla de la pareja #3 (rango 27300-27399)."*
- `src/nodes.c:121`: `port = base_port + 4u * id;`
- `dsm.sh:279` array `ports=(27300 27304 27308 27312)`
- `config.c:164-165` defaults: `port_range_start = 27300`, `port_range_end = 27399`
- `config.c:371-375` validación: todos los 4 puertos deben estar en `[port_range_start, port_range_end]` y ser distintos

### 5.2 Archivo de Nodos (`nodes.conf` / `nodes_N.conf`)
- **Formato**: `id host [puerto]` — una línea por nodo
- **Generación automática**: `dsm.sh:275-290` `write_nodes_file()` crea `nodes_${TOPOLOGY}.conf`
- **Edición manual**: `lab.sh:143-168` `ip` comando actualiza IP en `nodes.conf` y `nodes_2.conf`
- **Mismo archivo en todas las PCs**: `nodes.conf` idéntico en cada máquina (comentario `nodes.conf:4-5`)

### 5.3 Límites Prácticos
| Límite | Valor | Origen |
|--------|-------|--------|
| **PCs máximas** | **4** (A, B, C, D) | `dsm.sh:33-36` variables `IP_A`..`IP_D`, `TOPOLOGY` max 4 |
| **Nodos por PC** | **4** (puertos 27300-27315) | Rango de puertos 27300-27399 permite 25 nodos, pero scripts solo manejan 4 roles |
| **Nodos totales** | **16** (`DSM_MAX_NODES`) | `include/dsm_types.h:27` |
| **Nodos probados** | **4** (2 PCs × 2 nodos o 1 PC × 4 nodos) | `lab.sh` y `dsm.sh` solo testean hasta 4 |

---

## 6. ESCALABILIDAD: ¿Límite Artificial de 4 PCs? ¿16 Nodos? ¿Por Qué?

### 6.1 Límite de 4 PCs — **Sí, artificial en scripts y configuración**
| Componente | Límite | Evidencia |
|------------|--------|-----------|
| Variables IP en `dsm.sh` | 4 (`IP_A`, `IP_B`, `IP_C`, `IP_D`) | `dsm.sh:33-36` |
| Topología en `dsm.sh` | `read_number` 2–4 | `dsm.sh:599-600` |
| `peer_ips()` | Solo itera B, C, D | `dsm.sh:232-235` |
| `write_nodes_file()` | Hardcodea 4 IPs + 4 puertos base | `dsm.sh:276-288` |
| `cfg_for_role()` | Solo roles A, B, C, D | `dsm.sh:202-210` |
| `lab.sh` | `DEFAULT_NODES=4` | `lab.sh:36` |

**El código C soporta 16 nodos** (`DSM_MAX_NODES=16`), pero **la automatización (scripts) solo llega a 4**.

### 6.2 Límite de 16 Nodos — **Hardcodeado en C**
- `DSM_MAX_NODES = 16` en `include/dsm_types.h:27`
- Arrays fijos dimensionados a 16: `peer[16]`, `conn[16]`, `alive[16]`, `barrier_registered_round[16]`
- Bitmaps de `sharers` usan `uint64_t` → soporta **hasta 64 nodos** lógicamente, pero estructuras limitan a 16
- `dsm_config_validate()` permite `max_nodes` hasta 16 (línea 345: `node_id >= cfg->max_nodes`)

### 6.3 Razones del Límite 16
1. **Arrays estáticos** en estructuras globales (`dsm_nodes_t`, `dsm_net_t`, `dsm_coherence_t`, `dsm_heartbeat_t`)
2. **Bitmap de sharers** en `dsm_directory_entry_t` usa 64 bits → teóricamente 64, pero `DSM_MAX_NODES` limita a 16
3. **Barrier** usa array `[DSM_MAX_NODES]` para `barrier_registered_round`
4. **Sin asignación dinámica** de estructuras por nodo — todo estático en compile-time

### 6.4 Cuellos de Botella de Escalabilidad Real
| Cuello de Botella | Impacto |
|-------------------|---------|
| **Home único (nodo 0)** | Punto único de fallo y contención: todo `PAGE_REQUEST`, `OWNERSHIP_REQUEST`, `OBJECT_ALLOC`, `BARRIER` va al home |
| **Directorio centralizado** | Hash table en home — no particionado |
| **Invalidación broadcast** | `home_serve_write()` itera todos los `sharers` (hasta 64) enviando `INVALIDATE` secuencial |
| **Conexiones TCP persistentes** | Una por par de nodos → O(N²) conexiones; `DSM_NET_MAX_ACTIVE=64` limita a ~11 nodos (11×10/2=55) |
| **Pool de hilos fijo** | `DSM_NET_MAX_WORKERS=32` — no escala con nodos |
| **Bootstrap sincrónico** | `node_main.c:272-302` espera a **todos** peers (`all_alive`) con timeout global |

---

## 7. CONFIGURACIÓN POR COMPUTADORA: Archivos por Nodo vs Archivo Global

### 7.1 Estructura Actual
| Archivo | Alcance | Contenido |
|---------|---------|-----------|
| `config/node0.conf` | **Por nodo** (local a la PC) | `node_id`, puertos locales, `bind_address`, `log_file`, `seed_host` |
| `config/node1.conf` | **Por nodo** | Idem, `node_id=1`, puertos +4 |
| `config/node2.conf` | **Por nodo** | Idem, `node_id=2`, puertos +8 |
| `config/node3.conf` | **Por nodo** | Idem, `node_id=3`, puertos +12 |
| `config/nodes.conf` | **Global** (idéntico en todas las PCs) | Tabla completa: `id host puerto` de todos los nodos |
| `config/nodes_1/2/4.conf` | **Global** (plantillas por topología) | Subconjuntos para tests locales |

### 7.2 Diferencias Reales entre `node0.conf` y `node1.conf`

| Parámetro | `node0.conf` (Home) | `node1.conf` (Cliente) | Diferencia |
|-----------|---------------------|------------------------|------------|
| `node_id` | 0 | 1 | **Identidad** |
| `listen_port` | 27300 | 27304 | **+4** |
| `control_port` | 27301 | 27305 | **+4** |
| `heartbeat_port` | 27302 | 27306 | **+4** |
| `web_port` | 27303 | 27307 | **+4** |
| `seed_host` | 127.0.0.1 | 127.0.0.1 | Igual (apunta a home) |
| `seed_port` | 27300 | 27300 | Igual (puerto listen del home) |
| `log_file` | `logs/dsm-node0.log` | `logs/dsm-node1.log` | **Por nodo** |
| `bind_address` | 0.0.0.0 | 0.0.0.0 | Igual |
| **Resto** | Idéntico | Idéntico | 35+ parámetros iguales |

**Evidencia:** `diff config/node0.conf config/node1.conf` muestra solo las 8 líneas arriba.

### 7.3 Problemas de este Esquema
1. **Duplicación masiva**: 90%+ de parámetros idénticos en cada `node<N>.conf`
2. **Riesgo de deriva**: Cambiar `page_size` o `window_size` requiere editar 4 archivos
3. **Sin plantilla única**: No hay `config/common.conf` + overrides por nodo
4. **`seed_host/seed_port` redundantes**: Todos apuntan a home (nodo 0), pero se repiten
5. **`port_range_start/end` idénticos**: Hardcodeados en todos

### 7.4 Archivo Global (`nodes.conf`)
- **Única fuente de verdad** para topología
- **Debe ser idéntico en todas las PCs** (comentario `nodes.conf:4-5`)
- `dsm.sh` lo genera automáticamente desde `IP_A`..`IP_D` y `TOPOLOGY`
- `lab.sh ip` actualiza tanto `nodes.conf` como `nodes_2.conf` (líneas 152-167)

---

## 8. INCONSISTENCIAS: `node0.conf` vs `node1.conf` Diferencias Reales

### 8.1 Diferencias Intencionales (Correctas)
| Parámetro | Nodo 0 | Nodo 1 | Justificación |
|-----------|--------|--------|---------------|
| `node_id` | 0 | 1 | Identidad única |
| `listen_port` | 27300 | 27304 | Evitar colisión puertos |
| `control_port` | 27301 | 27305 | Idem |
| `heartbeat_port` | 27302 | 27306 | Idem |
| `web_port` | 27303 | 27307 | Idem |
| `log_file` | `dsm-node0.log` | `dsm-node1.log` | Logs separados |

### 8.2 Diferencias **Inconsistentes / Riesgosas** (No deberían variar)

| Parámetro | Valor en ambos | Riesgo si difieren |
|-----------|----------------|---------------------|
| `page_size` | 4096 | **Crítico**: `dsm_config_fingerprint()` lo incluye → `DSM_ERR_CONFIG_MISMATCH` |
| `node_memory` | 1048576 (1MB) | **Crítico**: Afecta `frame_count`, fingerprint |
| `window_size` | 268435456 (256MB) | **Crítico**: Fingerprint |
| `max_nodes` | 16 | **Crítico**: Fingerprint + validación `node_id < max_nodes` |
| `heartbeat_interval_ms` | 1000 | **Alto**: Timeout de red, fingerprint |
| `heartbeat_timeout_ms` | 3000 | **Alto**: Detección de fallos, fingerprint |
| `network_timeout_ms` | 10000 | **Alto**: RPC timeout, fingerprint |
| `connect_timeout_ms` | 5000 | **Alto**: Bootstrap, fingerprint |
| `retry_count` | 5 | **Medio**: Comportamiento reintentos |
| `retry_backoff_ms` | 500 | **Medio**: Backoff exponencial |
| `max_message_size` | 8192 | **Alto**: Límite protocolo, fingerprint |
| `thread_pool_size` | 32 | **Medio**: Capacidad concurrencia |
| `fault_queue_size` | 1024 | **Medio**: Cola fallos página |
| `eviction_policy` | 0 (LRU) | **Alto**: Comportamiento memoria, fingerprint |
| `log_level` | 2 (info) | Bajo (solo verbosidad local) |
| `stats_interval_ms` | 5000 | Bajo |
| `barrier_timeout_ms` | 30000 | **Alto**: Barreras, fingerprint |
| **FASE C timeouts** | Ver abajo | **Crítico**: Todos en fingerprint |

### 8.3 Parámetros FASE C (Nuevos en Prototipo 2) — **Todos en Fingerprint**
| Parámetro | Valor | En Fingerprint |
|-----------|-------|----------------|
| `bootstrap_timeout_ms` | 15000 | ✅ `config.c:467` |
| `shutdown_timeout_ms` | 5000 | ✅ `config.c:468` |
| `heartbeat_missed_max` | 3 | ✅ `config.c:469` |
| `rpc_max_retries` | 3 | ✅ `config.c:470` |
| `rpc_backoff_base_ms` | 100 | ✅ `config.c:471` |
| `rpc_max_total_ms` | 50000 (node0) / 50000 (node1) | ✅ `config.c:472` |

**Verificación:** `config.c:449-472` — `dsm_config_fingerprint()` mezcla **todos** estos parámetros (excepto `node_id` y `log_file`). **Cualquier diferencia → mismatch → nodo rechaza unirse al clúster.**

### 8.4 Parámetros **Fuera del Fingerprint** (Pueden diferir sin error)
- `node_id` — **Excluido intencionalmente** (comentario `config.h:69`)
- `log_file` — **Excluido intencionalmente** (comentario `config.h:69`)
- `bind_address` — **No incluido** (pero debería: afecta escucha)
- `seed_host`, `seed_port` — **No incluidos** (pero afectan bootstrap)

---

## 9. FINGERPRINT DE CONFIGURACIÓN: Qué Incluye, Qué Excluye

### 9.1 Algoritmo
- **FNV-1a 32-bit** sobre parámetros serializados + tabla de nodos ordenada por `node_id`
- Código: `config.c:437-497` función `dsm_config_fingerprint()`

### 9.2 Qué INCLUYE (Parámetros de `dsm_config_t`)
```c
cfg->config_version
cfg->page_size
cfg->node_memory (low + high 32 bits)
cfg->window_size (low + high 32 bits)
cfg->max_nodes
cfg->port_range_start
cfg->port_range_end
cfg->replication_factor
cfg->max_message_size
cfg->heartbeat_interval_ms
cfg->heartbeat_timeout_ms
cfg->network_timeout_ms
cfg->connect_timeout_ms
cfg->thread_pool_size
cfg->barrier_timeout_ms
// FASE C:
cfg->bootstrap_timeout_ms
cfg->shutdown_timeout_ms
cfg->heartbeat_missed_max
cfg->rpc_max_retries
cfg->rpc_backoff_base_ms
cfg->rpc_max_total_ms
DSM_PROTOCOL_VERSION (constante 2)
```

### 9.3 Qué INCLUYE (Tabla de Nodos — `dsm_nodes_t`)
Para **cada peer válido**, ordenado por `node_id`:
```c
peer->node_id
peer->port
peer->host (string, byte a byte en FNV)
```

### 9.4 Qué EXCLUYE (Intencional)
| Campo | Razón |
|-------|-------|
| `cfg->node_id` | Cada nodo tiene su propio ID; no debe causar mismatch |
| `cfg->log_file` | Ruta local de logs; irrelevante para compatibilidad |
| `cfg->bind_address` | **No incluido** — pero afecta `listen()` (riesgo silencioso) |
| `cfg->seed_host`, `cfg->seed_port` | **No incluidos** — afectan bootstrap (riesgo silencioso) |
| `cfg->listen_port`, `control_port`, `heartbeat_port`, `web_port` | **No incluidos directamente** — pero `port_range_start/end` y tabla de nodos (con puertos) sí lo están |
| `cfg->retry_count`, `retry_backoff_ms` | **No incluidos** — comportamiento local de reintentos |
| `cfg->fault_queue_size`, `eviction_policy`, `log_level`, `stats_interval_ms` | **No incluidos** — políticas locales |
| `cfg->replication_factor`, `checkpoint_interval_ms`, `scheduler_policy` | Incluidos parcialmente (`replication_factor` sí, otros no) |

### 9.5 Verificación en Red
- `net.c:144-147` — `cluster_hash` (fingerprint) se escribe en `reserved2` de la cabecera (offset 60, 4 bytes) en **cada mensaje saliente**
- Receptor puede validar `cluster_hash` recibido vs propio → detecta mismatch **antes** de procesar mensaje
- `dsm_protocol.h` no muestra validación explícita en `decode`, pero `net.c` la inyecta al enviar

### 9.6 Riesgos de lo Excluido
| Excluido | Riesgo Real |
|----------|-------------|
| `bind_address` | Nodo A escucha en `0.0.0.0`, Nodo B en `127.0.0.1` → fingerprint igual, pero B no recibe conexiones externas |
| `seed_host/port` | Nodos con seeds distintos → no encuentran home → timeout bootstrap silencioso |
| `retry_count/backoff` | Comportamiento asimétrico ante fallos transitorios |
| `eviction_policy` | Home usa LRU, cliente Clock → incoherencia en presión de memoria |

---

## RESUMEN EJECUTIVO DE HALLAZGOS CRÍTICOS

| # | Hallazgo | Severidad | Archivo/Línea |
|---|----------|-----------|---------------|
| 1 | **Home fijo en nodo 0 sin failover** | 🔴 Crítico | `runtime.c:161`, `coherence.c` |
| 2 | **Límite 4 PCs artificial en scripts** | 🟡 Medio | `dsm.sh:33-36, 232-240, 599-600` |
| 3 | **Límite 16 nodos hardcodeado en C** | 🟡 Medio | `include/dsm_types.h:27`, arrays estáticos |
| 4 | **Duplicación masiva en `node<N>.conf`** | 🟡 Medio | `config/node*.conf` (90% idénticos) |
| 5 | **Fingerprint excluye `bind_address` y `seed_*`** | 🟡 Medio | `config.c:449-472` |
| 6 | **Bootstrap sincrónico espera a TODOS** | 🟡 Medio | `node_main.c:272-302` |
| 7 | **Invalidación broadcast secuencial** | 🟡 Medio | `coherence.c:1045-1056` |
| 8 | **Sin reconfiguración dinámica de clúster** | 🟡 Medio | Ausente en toda la base |
| 9 | **`DSM_MAX_OBJECTS=128` límite bajo** | 🟢 Bajo | `include/dsm_coherence.h:79` |
| 10 | **Pool de hilos y conexiones fijos** | 🟢 Bajo | `net.c:43-45` |

---

## RECOMENDACIONES PRIORITARIAS

1. **Eliminar duplicación de config**: Crear `config/common.conf` + `config/node<N>.override` o usar variables de entorno.
2. **Añadir `bind_address` y `seed_*` al fingerprint** para detectar mismatches de red silenciosos.
3. **Diseñar failover de home** (elección automática, migración de directorio, o al menos documento que es limitación conocida).
4. **Generalizar scripts a N PCs** (array dinámico de IPs, no `IP_A`..`IP_D` fijos).
5. **Evaluar partición de directorio** (sharding por `page_id`) para escalar más allá de 16 nodos.
6. **Hacer bootstrap tolerante a joins tardíos** (no esperar a todos con `all_alive`).

---

*Fin del informe. Todas las citas referencian líneas exactas del código fuente en `Entrega_Final/codigo/`.*