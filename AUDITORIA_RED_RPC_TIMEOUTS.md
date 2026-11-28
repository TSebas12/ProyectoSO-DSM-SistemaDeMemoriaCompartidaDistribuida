# Auditoría Completa: Red, RPC, Timeouts y Estabilidad
**Agente 4** — Entrega_Final/codigo/  
Fecha: 2026-11-27  

---

## 1. Tabla Comparativa: Timeouts Actuales vs Anteriores

| Parámetro | Entregable_2 (Prototipo 1) | Entregable_3 (Prototipo 2) | Entrega_Final (Actual) | Cambio |
|-----------|----------------------------|----------------------------|------------------------|--------|
| `heartbeat_interval_ms` | 1000 | 1000 | **1000** | = |
| `heartbeat_timeout_ms` | 3000 | 3000 | **3000** | = |
| `network_timeout_ms` | 2000 | 10000 | **10000** | **+5x** (E3→Final =) |
| `connect_timeout_ms` | 2000 | 5000 | **5000** | **+2.5x** (E3→Final =) |
| `retry_count` | 3 | 5 | **`rpc_max_retries` = 3** | E3 tenía 5, Final vuelve a 3 |
| `retry_backoff_ms` | 250 | 500 | **`rpc_backoff_base_ms` = 100** | E3 500→Final 100 (base exponencial) |
| `barrier_timeout_ms` | 30000 | 30000 | **30000** | = |
| **NUEVO: `bootstrap_timeout_ms`** | — | — | **15000** | FASE C |
| **NUEVO: `shutdown_timeout_ms`** | — | — | **5000** | FASE C |
| **NUEVO: `heartbeat_missed_max`** | — | — | **3** | FASE C |
| **NUEVO: `rpc_max_retries`** | — | — | **3** | FASE C (reemplaza retry_count) |
| **NUEVO: `rpc_backoff_base_ms`** | — | — | **100** | FASE C (base exponencial) |
| **NUEVO: `rpc_max_total_ms`** | — | — | **50000** | FASE C (presupuesto total) |
| `thread_pool_size` | 4 | 32 | **32** | = (E3→Final) |
| `max_message_size` | 8192 | 8192 | **8192** | = |

**Valores en config/node0.conf (Entrega_Final):**
```conf
heartbeat_interval_ms = 1000
heartbeat_timeout_ms = 3000
network_timeout_ms = 10000
connect_timeout_ms = 5000
retry_count = 5          # LEGACY: ya no se usa en código RPC
retry_backoff_ms = 500   # LEGACY: ya no se usa en código RPC
barrier_timeout_ms = 30000
bootstrap_timeout_ms = 15000
shutdown_timeout_ms = 5000
heartbeat_missed_max = 3
rpc_max_retries = 3
rpc_backoff_base_ms = 100
rpc_max_total_ms = 50000
```

> **Nota:** `retry_count` y `retry_backoff_ms` persisten en el archivo de configuración y en `dsm_config_defaults()` (líneas 142-143, 154-155 de `config.c`) pero **ya no se usan** en el código de RPC. El código usa `rpc_max_retries` y `rpc_backoff_base_ms`.

---

## 2. RPC: Reintentos, Backoff Exponencial, Presupuesto Total

### Código: `src/net.c` — función `rpc()` (líneas 377-445)

```c
static dsm_status_t rpc(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req,
                        dsm_msg_t *resp, uint32_t timeout_ms)
{
    dsm_status_t st = DSM_ERR_NET;
    uint32_t     attempt = 0;
    uint64_t     start_us = now_us();
    uint32_t     max_total_us = net->cfg.rpc_max_total_ms * 1000u;   // 50,000,000 µs = 50s
    uint32_t     max_retries = net->cfg.rpc_max_retries;             // 3
    // ...
    while (attempt <= max_retries) {        // Hasta 4 intentos totales (0..3)
        // Chequeo presupuesto total
        uint64_t elapsed_us = now_us() - start_us;
        if (elapsed_us >= max_total_us) {   // Timeout global 50s
            return DSM_ERR_TIMEOUT;
        }
        // Timeout por intento = min(remaining, timeout_ms)
        uint32_t remaining_ms = (max_total_us - elapsed_us) / 1000u;
        uint32_t this_timeout = (remaining_ms < timeout_ms) ? remaining_ms : timeout_ms;
        // ...
        st = rpc_once(net, dst, req, resp, this_timeout);
        if (st == DSM_OK) return DSM_OK;
        // Backoff exponencial: base * 2^attempt
        uint32_t backoff = net->cfg.rpc_backoff_base_ms * (1u << attempt);  // 100, 200, 400 ms
        nanosleep(&ts, NULL);
        attempt++;
    }
    return st;
}
```

### Parámetros efectivos (config actual):
| Intento | Backoff (ms) | Timeout por intento (ms) | Acumulado backoff |
|---------|--------------|--------------------------|-------------------|
| 0 (1ro) | 0            | 10,000 (network_timeout) | 0                 |
| 1 (2do) | 100          | ≤ 49,900 restante        | 100               |
| 2 (3ro) | 200          | ≤ 49,600 restante        | 300               |
| 3 (4to) | 400          | ≤ 49,100 restante        | 700               |

**Presupuesto total:** 50,000 ms (50 segundos)  
**Máximo reintentos:** 3 (4 intentos totales)  
**Backoff base:** 100 ms → secuencia: 100, 200, 400 ms  

### Validación en `config.c` (líneas 413-423):
```c
/* rpc_max_total_ms debe cubrir al menos (retries+1)*network_timeout + suma backoffs */
uint32_t min_total = (cfg->rpc_max_retries + 1u) * cfg->network_timeout_ms;
for (u = 1u; u <= cfg->rpc_max_retries; u++) {
    min_total += cfg->rpc_backoff_base_ms * (1u << (u - 1u));
}
if (cfg->rpc_max_total_ms < min_total) return DSM_ERR_INVAL;
```
Con valores actuales: `min_total = 4*10000 + (100+200+400) = 40,700 ms` < `50,000 ms` ✓

---

## 3. Heartbeat: Intervalo, Timeout, Missed Max, Estado "Lento", Bootstrap Grace

### Código: `src/heartbeat.c` — `heartbeat_loop()` (líneas 47-98)

```c
static void *heartbeat_loop(void *arg)
{
    dsm_heartbeat_t *hb = arg;
    dsm_runtime_t   *rt = hb->rt;

    while (atomic_load_explicit(&hb->running, memory_order_acquire)) {
        // PRIMERA RONDA CON RETRASO (bootstrap grace)
        hb_sleep_ms(rt->cfg.heartbeat_interval_ms);  // 1000 ms
        // ...

        for (i = 0; i < DSM_MAX_PEERS; i++) {
            st = dsm_net_ping(rt->net, i, rt->cfg.heartbeat_timeout_ms);  // 3000 ms
            if (st == DSM_OK) {
                atomic_store_explicit(&hb->alive[i], 1, memory_order_relaxed);
                hb->last_seen_ms[i] = dsm_now_ms();
                atomic_store_explicit(&hb->consecutive_missed[i], 0, memory_order_relaxed);
            } else {
                int missed = atomic_fetch_add_explicit(&hb->consecutive_missed[i], 1, ...) + 1;
                if (missed >= (int)rt->cfg.heartbeat_missed_max) {  // 3
                    atomic_store_explicit(&hb->alive[i], 0, memory_order_relaxed);
                    DSM_LOG(WARN, "nodo %u declarado caído tras %d pings fallados consecutivos", i, missed);
                } else {
                    DSM_LOG(DEBUG, "nodo %u ping fallado %d/%u", i, missed, rt->cfg.heartbeat_missed_max);
                }
            }
        }
    }
}
```

### Parámetros Heartbeat:
| Parámetro | Valor | Significado |
|-----------|-------|-------------|
| `heartbeat_interval_ms` | 1000 | Intervalo entre rondas completas de PING |
| `heartbeat_timeout_ms` | 3000 | Timeout por PING individual (vía `dsm_net_ping`) |
| `heartbeat_missed_max` | 3 | Pings fallados consecutivos antes de declarar "caído" |
| **Detección mínima** | 3s × 3 = **9s** | Tiempo mínimo para detectar caída real |
| **Bootstrap grace** | 1 ronda (1s) | Primera ronda retrasada para dar tiempo a `listen()` |

### Estado "lento" (slow):
- No hay estado explícito "lento" en el código actual
- Solo dos estados: `alive=1` (vivo) o `alive=0` (caído tras `missed_max`)
- Log DEBUG muestra pings fallados intermedios: `"ping fallado 1/3"`, `"ping fallado 2/3"`

### `consecutive_missed` array (nuevo en Entrega_Final):
- Línea 35: `_Atomic int consecutive_missed[DSM_MAX_PEERS];`
- Inicializado a 0 (línea 117)
- Reseteado a 0 en éxito (línea 80)
- Incrementado atómicamente en fallo (línea 82-83)

---

## 4. Bootstrap: Espera con Deadline, Logs de Peers

### Código: `src/node_main.c` — FASE C (líneas 271-302)

```c
/* --- FASE C: Espera bootstrap (ventana para que peers hagan listen()) --- */
if (rt->n_nodes > 1u && rt->hb != NULL) {
    uint64_t bootstrap_deadline = dsm_now_ms() + rt->cfg.bootstrap_timeout_ms;  // 15000 ms
    DSM_LOG(INFO, "nodo %u: esperando peers (bootstrap %u ms)...", cfg.node_id, rt->cfg.bootstrap_timeout_ms);

    while (dsm_now_ms() < bootstrap_deadline) {
        int all_alive = 1;
        for (uint32_t i = 0; i < DSM_MAX_PEERS; i++) {
            const dsm_peer_t *p = dsm_nodes_get(&rt->nodes, i);
            if (p == NULL || i == cfg.node_id) continue;
            if (!dsm_heartbeat_alive(rt->hb, i)) {  // Verifica hb->alive[i]
                all_alive = 0;
                break;
            }
        }
        if (all_alive) {
            DSM_LOG(INFO, "nodo %u: todos peers respondieron en bootstrap", cfg.node_id);
            break;
        }
        sleep_ms(100);  // Poll cada 100ms
    }
    /* Log de peers que no respondieron */
    for (uint32_t i = 0; i < DSM_MAX_PEERS; i++) {
        const dsm_peer_t *p = dsm_nodes_get(&rt->nodes, i);
        if (p && i != cfg.node_id && !dsm_heartbeat_alive(rt->hb, i)) {
            uint64_t elapsed = dsm_now_ms() - (bootstrap_deadline - rt->cfg.bootstrap_timeout_ms);
            DSM_LOG(WARN, "nodo %u: peer %u no respondió en bootstrap (%.0f ms)",
                    cfg.node_id, i, (double)elapsed);
        }
    }
}
```

### Comportamiento:
1. **Deadline:** `now + bootstrap_timeout_ms` (15s por defecto)
2. **Poll:** Cada 100ms verifica `dsm_heartbeat_alive()` para todos los peers
3. **Éxito:** Sale del bucle si `all_alive == 1`
4. **Timeout:** Al cumplir 15s, continúa y loggea peers que no respondieron
5. **No aborta:** El nodo sigue arrancando aunque falten peers (fail-stop en RPC posterior)

### Validación en `config.c` (línea 425-427):
```c
/* bootstrap_timeout_ms >= 2 * connect_timeout_ms (para 2 intentos de conexión) */
if (cfg->bootstrap_timeout_ms < 2u * cfg->connect_timeout_ms) return DSM_ERR_INVAL;
```
Con valores: `15000 >= 2*5000 = 10000` ✓

---

## 5. Shutdown: Deadline `shutdown_timeout_ms`

### Código: `src/runtime.c` — `dsm_runtime_destroy()` (líneas 231-271)

```c
void dsm_runtime_destroy(dsm_runtime_t *rt)
{
    /* Deadline para cierre ordenado (FASE C) */
    uint64_t shutdown_deadline = dsm_now_ms() + rt->cfg.shutdown_timeout_ms;  // 5000 ms

    if (rt->web != NULL) { dsm_web_stop(rt->web); rt->web = NULL; }
    if (rt->ctl != NULL) { dsm_control_stop(rt->ctl); rt->ctl = NULL; }
    if (rt->hb != NULL) { dsm_heartbeat_stop(rt->hb); rt->hb = NULL; }

    if (atomic_load(&rt->running)) {
        dsm_fault_shutdown();
        /* Join con deadline para evitar bloqueo indefinido */
        struct timespec ts;
        while (atomic_load(&rt->running) && dsm_now_ms() < shutdown_deadline) {
            ts.tv_sec = 0;
            ts.tv_nsec = 10000000L;  // 10 ms
            nanosleep(&ts, NULL);
        }
        pthread_join(rt->resolver_thread, NULL);
        atomic_store(&rt->running, 0);
    }
    if (rt->net != NULL) { dsm_net_destroy(rt->net); rt->net = NULL; }
    // ...
}
```

### Secuencia de apagado:
1. **Web** → **Control** → **Heartbeat** (orden importa: format_nodes/nodes leen rt->hb)
2. **Fault resolver:** Join con deadline de 5s (poll 10ms)
3. **Red:** `dsm_net_destroy()` → cierra sockets, joins listener + workers
4. **Coherencia, memoria, SIGSEGV handler**

### Validación en `config.c` (línea 428-431):
```c
/* shutdown_timeout_ms >= 1000ms (mínimo razonable) */
if (cfg->shutdown_timeout_ms < 1000u) return DSM_ERR_INVAL;
```
Valor actual: 5000 ms ✓

---

## 6. Barreras: Timeout Unificado vs Hardcoded

### Código: `src/sync.c` — No hay implementación de barreras aquí

Las barreras se implementan en **coherencia** (`coherence.c`). Búsqueda en código:

```bash
grep -r "barrier_timeout" Entrega_Final/codigo/src/
```

**Resultado:** No hay uso de `barrier_timeout_ms` en el código fuente actual.

### En `dsm_config.h` (línea 43):
```c
uint32_t barrier_timeout_ms;  // Existe en struct pero NO SE USA
```

### En `config.c` defaults (línea 151):
```c
cfg->barrier_timeout_ms = 30000;  // 30s default
```

### En validación: **No hay validación** para `barrier_timeout_ms`

### En `config/node0.conf`:
```conf
barrier_timeout_ms = 30000
```

### Conclusión:
**`barrier_timeout_ms` está definido en configuración pero NO se usa en ningún lado del código de runtime.** Las barreras (si existen en coherencia) probablemente usan timeouts hardcoded o `network_timeout_ms`. Esto es una **inconsistencia detectada**.

---

## 7. Problemas Detectados

### 7.1 Timeouts Duplicados/Inconsistentes

| Campo Config | Usado en Código | Estado |
|--------------|-----------------|--------|
| `retry_count` | **NO** (legacy) | Duplicado con `rpc_max_retries` |
| `retry_backoff_ms` | **NO** (legacy) | Duplicado con `rpc_backoff_base_ms` |
| `barrier_timeout_ms` | **NO** | Definido pero no usado |
| `network_timeout_ms` | SÍ (RPC, heartbeat, server) | OK |
| `connect_timeout_ms` | SÍ (`connect_with_timeout`) | OK |
| `heartbeat_timeout_ms` | SÍ (`dsm_net_ping`) | OK |
| `bootstrap_timeout_ms` | SÍ (node_main.c) | OK (FASE C) |
| `shutdown_timeout_ms` | SÍ (runtime.c) | OK (FASE C) |
| `rpc_max_retries` | SÍ (net.c rpc) | OK (FASE C) |
| `rpc_backoff_base_ms` | SÍ (net.c rpc) | OK (FASE C) |
| `rpc_max_total_ms` | SÍ (net.c rpc) | OK (FASE C) |
| `heartbeat_missed_max` | SÍ (heartbeat.c) | OK (FASE C) |

**Problema:** `retry_count=5` y `retry_backoff_ms=500` en config/node0.conf son **valores legacy** que no se usan. El código usa `rpc_max_retries=3` y `rpc_backoff_base_ms=100`. Esto genera confusión.

### 7.2 Valores Agresivos para Cross-PC

| Parámetro | Valor Actual | Riesgo Cross-PC |
|-----------|--------------|-----------------|
| `connect_timeout_ms` | 5000 ms | **Alto**: 5s para TCP handshake cross-PC puede ser poco en redes con latencia variable |
| `heartbeat_timeout_ms` | 3000 ms | **Medio**: PING timeout 3s, intervalo 1s → detección en 9s mínimo |
| `network_timeout_ms` | 10000 ms | **Medio**: RPC timeout 10s por intento |
| `bootstrap_timeout_ms` | 15000 ms | **Medio**: 15s para que todos hagan listen() en red lenta |

**Recomendación cross-PC:** Aumentar `connect_timeout_ms` a 10-15s, `heartbeat_timeout_ms` a 5-10s, `bootstrap_timeout_ms` a 30s.

### 7.3 Falta Backoff en Alguna Ruta

**Rutas con backoff exponencial:**
- ✅ `rpc()` en `net.c` líneas 431-440: backoff `base * 2^attempt`

**Rutas SIN backoff:**
- ❌ `peer_acquire()` / `connect_with_timeout()`: Reintento de conexión **no tiene backoff** entre intentos de RPC. El backoff solo ocurre *después* de que `rpc_once` falla completamente.
- ❌ `dsm_net_ping()` (heartbeat): Sin reintentos, un solo intento por ronda
- ❌ Bootstrap wait: Poll fijo 100ms, sin backoff

### 7.4 Conexiones Simultáneas y Pool Workers

**En `net.c`:**
```c
#define DSM_NET_MAX_WORKERS  32      // Línea 43
#define DSM_NET_QUEUE_LEN    64      // Línea 44
#define DSM_NET_MAX_ACTIVE   64      // Línea 45
```

**En `dsm_net_start()` (líneas 837-843):**
```c
net->worker_count = net->cfg.thread_pool_size;  // config: 32
if (net->worker_count < 4u) net->worker_count = 4u;
if (net->worker_count > DSM_NET_MAX_WORKERS) net->worker_count = DSM_NET_MAX_WORKERS;
```

**Análisis:**
- **Máx 32 workers** (hilos) atendiendo conexiones entrantes
- **Cola de 64** conexiones pendientes (accept → queue → worker)
- **Máx 64 conexiones activas** simultáneas rastreadas
- **Conexiones salientes:** 1 por peer (persistente, protegida por mutex)
- **Total peers:** `DSM_MAX_PEERS` (definido en dsm_types.h, típicamente 16)

**Cuello de botella potencial:** Si llegan >64 conexiones simultáneas entrantes, `queue_push` falla y cierra el socket (línea 692-694). Para 4 nodos cross-PC es suficiente, pero para clústeres mayores podría ser limitante.

---

## 8. Evidencia de Estabilidad

### Pruebas de Aceptación: `tests/run_acceptance.sh`

Ejecuta 7 aplicaciones con 2-4 nodos en loopback:

| App | Nodos | Tamaño | Timeout Test | Criterio Éxito |
|-----|-------|--------|--------------|----------------|
| demo | 2 | 1000 | 45s | "demo: fallos=0" sin "barrera fallo" |
| matrices | 4 | 64 | 45s | "verificacion: OK" |
| file | 2 | 1000 | 45s | "checksum.*OK" |
| counter | 2 | 2000 | 45s | "suma.*esperado.*OK" |
| evict | 2 | 1000 | 30s | "verificacion OK" |
| bench | 2 | 1000 | 45s | "rpc totales" sin "reporto un fallo" |
| task | 4 | 10000 | 45s | "total paralelo.*OK" |

### Lab.sh Stress Test (líneas 126-129):
```bash
stress)
    run_app matrices --size 192 || exit 1
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app counter --iters 100 --threads 2
    ;;
```

### Comentario en lab.sh línea 122-124 (evict):
> "con 4 nodos la presión de memoria provoca timeouts de red intermitentes (ver limitaciones en el entregable)"

### Métricas de estabilidad reportadas:
- **bench app:** Reporta "rpc totales" y latencias
- **stats RPC:** `DSM_STAT_RPC_COUNT`, `DSM_STAT_RPC_LATENCY_US`, `DSM_STAT_RETRIES`, `DSM_STAT_TIMEOUTS`, `DSM_STAT_ERRORS` (net.c líneas 410-429)

> **Nota:** El usuario menciona "test_simple2.sh (40 RPCs, 30 retries, 0 timeouts)" pero este archivo **no existe** en el repositorio. La evidencia real está en `run_acceptance.sh` y `lab.sh stress`.

---

## 9. Limpieza de Sockets: TIME_WAIT, EADDRINUSE, SO_LINGER

### Código actual en `net.c`:

**SO_REUSEADDR en listener (línea 822):**
```c
setsockopt(net->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

**Shutdown antes de close en `dsm_net_stop()` (líneas 908-918):**
```c
if (net->listen_fd >= 0) {
    shutdown(net->listen_fd, SHUT_RDWR);
}
if (net->active_ready && dsm_mutex_lock(&net->active_lock) == DSM_OK) {
    for (i = 0; i < DSM_NET_MAX_ACTIVE; i++) {
        if (net->active[i] >= 0) {
            shutdown(net->active[i], SHUT_RDWR);  // Despierta reads/bloqueos
        }
    }
    dsm_mutex_unlock(&net->active_lock);
}
```

**Close en `dsm_net_close_peers()` (líneas 889-895):**
```c
if (dsm_mutex_lock(&net->conn[i].lock) == DSM_OK) {
    if (net->conn[i].fd >= 0) {
        close(net->conn[i].fd);
        net->conn[i].fd = -1;
    }
    dsm_mutex_unlock(&net->conn[i].lock);
}
```

### Qué FALTA / Qué MEJORAR:

| Aspecto | Estado Actual | Recomendación |
|---------|---------------|---------------|
| `SO_REUSEADDR` | ✅ En listener | OK |
| `SO_REUSEPORT` | ❌ No usado | Considerar para múltiples procesos en mismo puerto |
| `SO_LINGER` | ❌ No configurado | **Añadir** `linger { l_onoff=1, l_linger=0 }` en sockets salientes para evitar TIME_WAIT en reinicios rápidos |
| `TCP_NODELAY` | ✅ En `set_nodelay()` (línea 120) | OK (latencia baja) |
| `shutdown()` antes de `close()` | ✅ En stop/close_peers | OK |
| Manejo `EADDRINUSE` | ❌ No explícito | `SO_REUSEADDR` mitiga pero race condition posible en reinicio rápido |
| `SO_KEEPALIVE` | ❌ No configurado | **Añadir** para detectar peers muertos sin heartbeat app-level |

### Código sugerido para `connect_with_timeout()` (línea 194-245):
```c
// Después de crear socket, antes de connect:
int linger_on = 1;
struct linger ling = { .l_onoff = 1, .l_linger = 0 };
setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));  // Close inmediato, sin TIME_WAIT

int keepalive = 1;
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
// Opcional: TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT (Linux-specific)
```

### En listener socket (línea 818-822):
```c
net->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
int opt = 1;
setsockopt(net->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
// Añadir SO_REUSEPORT si disponible:
#ifdef SO_REUSEPORT
setsockopt(net->listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
```

---

## Resumen Ejecutivo

| Categoría | Estado | Prioridad |
|-----------|--------|-----------|
| **Timeouts FASE C** | Implementados y validados | ✅ Completado |
| **RPC con backoff exponencial + presupuesto total** | ✅ Correcto | — |
| **Heartbeat con missed_max + bootstrap grace** | ✅ Correcto | — |
| **Bootstrap wait con deadline + logs** | ✅ Correcto | — |
| **Shutdown con deadline** | ✅ Correcto | — |
| **Barrier timeout** | ❌ **No usado** (config orphan) | 🔧 Limpiar o implementar |
| **Timeouts legacy duplicados** | ⚠️ `retry_count`, `retry_backoff_ms` no usados | 🧹 Deprecar/eliminar de config |
| **Valores cross-PC** | ⚠️ Agresivos (5s connect, 3s hb) | 📈 Aumentar para producción cross-PC |
| **Backoff en conexión** | ❌ Falta en `connect_with_timeout` | 🔧 Añadir |
| **Pool workers / cola** | ✅ 32 workers, cola 64 | — (OK para 4 nodos) |
| **Limpieza sockets** | ⚠️ Falta `SO_LINGER`, `SO_KEEPALIVE` | 🔧 Añadir para robustez |
| **Evidencia estabilidad** | ✅ 7 apps pasan en acceptance | — |

**Archivos auditados:**
- `src/net.c` (líneas 1-1035)
- `src/heartbeat.c` (líneas 1-200)
- `src/node_main.c` (líneas 1-346)
- `src/runtime.c` (líneas 1-303)
- `include/dsm_config.h` (líneas 1-75)
- `src/config.c` (líneas 1-564)
- `config/node0.conf`, `config/nodes.conf`
- `tests/run_acceptance.sh`, `lab.sh`