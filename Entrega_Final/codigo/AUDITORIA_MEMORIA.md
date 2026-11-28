# Auditoría Completa del Subsistema de Memoria — Entrega_Final/codigo/

**Agente:** AGENTE 3 — Memoria, páginas, marcos y límites  
**Fecha:** 2026-11-27  
**Alcance:** `include/dsm_memory.h`, `include/dsm_config.h`, `src/memory.c`, `src/config.c`, `src/coherence.c`, `src/fault.c`, `config/node*.conf`

---

## 1. Modelo de Memoria Verificado Matemáticamente

### Parámetros de Configuración (node*.conf, líneas 15-17)

| Parámetro | Valor | Fuente |
|-----------|-------|--------|
| `page_size` | 4096 bytes | `config/node*.conf:15` |
| `node_memory` | 1,048,576 bytes (1 MB) | `config/node*.conf:16` |
| `window_size` | 268,435,456 bytes (256 MB) | `config/node*.conf:17` |

### Cálculos Derivados (memory.c:119, runtime.c:131)

```c
// memory.c:119
mem->page_count = (uint32_t)(mem->window_size / (size_t)mem->page_size);
// = 268435456 / 4096 = 65,536 páginas virtuales por nodo

// runtime.c:131
mp.frame_count = (uint32_t)(cfg->node_memory / cfg->page_size);
// = 1048576 / 4096 = 256 marcos físicos por nodo
```

### Capacidades Agregadas (4 nodos)

| Métrica | Por Nodo | Agregado (4 nodos) |
|---------|----------|-------------------|
| Memoria física (marcos) | 256 × 4 KB = **1 MB** | **4 MB** |
| Ventana virtual | 65,536 × 4 KB = **256 MB** | **1 GB** |
| Ratio overcommit | 256:1 | 256:1 |
| Máx. páginas virtuales | 65,536 | 262,144 |
| Máx. asignaciones concurrentes | 256 residentes | 1,024 residentes |

### Verificación de Límites (config.c:349-364)

```c
// config.c:349-358
if (cfg->page_size < 4096u || (cfg->page_size & (cfg->page_size - 1u)) != 0u) return DSM_ERR_INVAL;
if (cfg->node_memory % cfg->page_size != 0u) return DSM_ERR_INVAL;
if (cfg->node_memory / cfg->page_size < 16u) return DSM_ERR_INVAL;  // Mínimo 16 marcos
if (cfg->window_size < cfg->node_memory) return DSM_ERR_INVAL;       // Ventana ≥ memoria física
if (cfg->window_size % cfg->page_size != 0u) return DSM_ERR_INVAL;
```

✅ **Validación correcta**: Todos los invariantes se cumplen con los valores por defecto.

---

## 2. Límites y Validaciones en Funciones Críticas

### `dsm_memory_init` (memory.c:74-195)

| Validación | Línea | Comportamiento |
|------------|-------|----------------|
| `mem != NULL` | 80-82 | `DSM_ERR_INVAL` |
| `sysconf(_SC_PAGESIZE) > 0` | 93-96 | `DSM_ERR_INVAL` |
| `page_size ≥ sys_page && page_size % sys_page == 0` | 105-108 | `DSM_ERR_INVAL` |
| `window_size % page_size == 0` | 113-115 | `DSM_ERR_INVAL` |
| `window_size / page_size ≤ UINT32_MAX` | 116-118 | `DSM_ERR_OVERFLOW` |
| `frame_count > 0` (default 256) | 100 | Default hardcodeado |
| `bucket_count > 0` (default 1024) | 101-102 | Default hardcodeado |

**Hallazgo**: `frame_count` no se valida contra `page_count` (podría ser > páginas virtuales). Se usa `calloc` sin comprobar overflow de `frame_count * sizeof(dsm_frame_t)` (línea 160).

### `dsm_memory_alloc_frame` (memory.c:504-531)

```c
// Línea 519-521: pool agotado
if (mem->free_top == 0) {
    dsm_mutex_unlock(&mem->frame_lock);
    return DSM_INVALID_FRAME;  // No bloquea, falla rápido
}
```

- **No hay espera**: Retorna inmediatamente `DSM_INVALID_FRAME` si no hay marcos libres.
- **Caller debe reintentar**: `coherence_make_room` (coherence.c:794-842) llama en bucle hasta 64 veces.

### `dsm_memory_page_get_or_create` (memory.c:318-394)

| Validación | Línea | Comportamiento |
|------------|-------|----------------|
| `mem != NULL && initialized` | 326-328 | `NULL` |
| `dsm_memory_page_addr != NULL` | 329-332 | `NULL` (page_id fuera de ventana) |
| `calloc` falla | 351-354 | `NULL` |
| `mutex_init` / `cond_init` fallan | 366-371 | Limpieza + `NULL` |
| Carrera de inserción | 380-387 | Reintenta lookup, libera `nuevo`, retorna existente |

**Race condition**: Se suelta `table_lock` para hacer `calloc` (línea 349). Otro hilo puede crear la misma página. Se maneja con re-check bajo lock (líneas 380-387) ✅.

---

## 3. Desbordamientos Aritméticos

### Multiplicaciones

| Ubicación | Expresión | Riesgo | Mitigación |
|-----------|-----------|--------|------------|
| memory.c:119 | `window_size / page_size` | División, seguro | Validación línea 116-118 |
| memory.c:161 | `frame_count * sizeof(dsm_frame_t)` | **Overflow si frame_count grande** | `frame_count` viene de config (máx 1M/4K=256) |
| memory.c:161 | `frame_count * sizeof(uint32_t)` | **Overflow si frame_count grande** | Igual |
| coherence.c:703 | `(page_id - page_base_id) * page_size` | **Offset swap 64-bit** | `page_id < page_base_id + page_count` validado en `backing_page_addr` |
| fault.c:86 | `(a - base) / page_size` | Punteros `uintptr_t` | Correcto en x86-64 |
| runtime.c:131 | `node_memory / page_size` | División entera | Validado en `config_validate` |

### Conversiones Implícitas

| Ubicación | Conversión | Riesgo |
|-----------|------------|--------|
| memory.c:98 | `p->page_size == 0 ? DSM_PAGE_SIZE_DEFAULT : p->page_size` | `size_t` → `uint32_t` (OK, page_size ≤ 64K) |
| memory.c:119 | `(uint32_t)(window_size / page_size)` | `size_t` → `uint32_t` (validado ≤ UINT32_MAX) |
| coherence.c:703 | `(off_t)((page_id - page_base_id) * (uint64_t)page_size)` | `uint64_t` → `off_t` (OK, offset < 256MB) |
| stats.c:159 | `total / count` | División entera, truncamiento aceptable |

**Hallazgo Crítico**: En `memory.c:160-161`, `calloc(mem->frame_count, sizeof(dsm_frame_t))` no verifica overflow de la multiplicación. Con `frame_count = 256` es seguro, pero si config permitiera valores mayores (p.ej. 1 GB / 4 KB = 262,144), `262144 * 40 = 10,485,760` bytes — aún cabe en `size_t`. **Riesgo bajo con valores actuales**.

---

## 4. Swap: Ruta, Tamaño, Inicialización, Escritura, Lectura, Consistencia

### Ruta y Tamaño (memory.c:648-671, runtime.c:95-108)

```c
// runtime.c:103-104
snprintf(path, sizeof(path), "logs/swap-node%u.bin", rt->cfg.node_id);
// → logs/swap-node0.bin, swap-node1.bin, etc.

// memory.c:664
if (ftruncate(fd, (off_t)mem->window_size) != 0)
// Tamaño = window_size = 256 MB (archivo disperso)
```

**Archivos observados**: 268,435,456 bytes cada uno (256 MB) ✅ coincide con `window_size`.

### Inicialización

```c
// runtime.c:95-108: runtime_open_swap()
mkdir("logs", 0755);
snprintf(path, ..., "logs/swap-node%u.bin", node_id);
dsm_memory_swap_open(&rt->mem, path);
```

- Se llama **después** de `dsm_memory_init` (línea 139).
- Si falla: **solo warning**, expulsión en home queda deshabilitada (línea 105-106).

### Escritura (`dsm_memory_swap_out`, memory.c:690-711)

```c
addr = backing_page_addr(mem, page_id);  // Vista RW del mismo memfd
off = (off_t)((page_id - mem->page_base_id) * (uint64_t)mem->page_size);
n = pwrite(mem->swap_fd, addr, (size_t)mem->page_size, off);
if (n != (ssize_t)mem->page_size) return DSM_ERR_NET;
atomic_fetch_add_explicit(&mem->swap_bytes, (uint64_t)mem->page_size, memory_order_relaxed);
```

- Escribe **página completa** (4 KB) atómicamente via `pwrite`.
- Incrementa `swap_bytes` atómicamente.

### Lectura (`dsm_memory_swap_in`, memory.c:713-731)

```c
addr = backing_page_addr(mem, page_id);
off = (off_t)((page_id - mem->page_base_id) * (uint64_t)mem->page_size);
n = pread(mem->swap_fd, addr, (size_t)mem->page_size, off);
if (n != (ssize_t)mem->page_size) return DSM_ERR_NET;
```

- Lee **página completa** via `pread`.
- **No decrementa `swap_bytes`** → contador es **bytes escritos acumulados**, no uso actual.

### Consistencia

| Aspecto | Implementación | Evaluación |
|---------|----------------|------------|
| Atomicidad escritura | `pwrite` atómico por página | ✅ Correcto |
| Atomicidad lectura | `pread` atómico por página | ✅ Correcto |
| Flag de página en swap | `DSM_PAGE_FLAG_SWAP` (memory.h:84) | ✅ Se setea en `coherence_evict_one:770` |
| Limpieza flag | `p->meta.flags &= ~DSM_PAGE_FLAG_SWAP` (memory.c:467, coherence.c:611) | ✅ Al hacer swap_in o install |
| Contador `swap_bytes` | Solo incrementa en swap_out | ⚠️ **No refleja uso actual**, solo total histórico |
| Tamaño archivo swap | `window_size` (256 MB) vs `frame_count * page_size` (1 MB) | ⚠️ **Oversized 256x** — archivo disperso OK en Linux, pero `ftruncate` reserva espacio en algunos FS |

**Inconsistencia**: `swap_bytes` en `dsm_runtime_refresh_stats` (runtime.c:286-287) lee `atomic_load(&rt->mem.swap_bytes)` → reporta **total histórico**, no bytes actualmente en swap.

---

## 5. Políticas de Reemplazo

### LRU Aproximado (memory.c:603-645)

```c
uint64_t dsm_memory_pick_victim(dsm_memory_t *mem, uint32_t min_age_ms, uint64_t avoid_page)
{
    now = dsm_now_ms();
    for (i = 0; i < mem->frame_count; i++) {
        if (mem->frames[i].flags != DSM_FRAME_USED) continue;
        if (mem->frames[i].page_id == avoid_page) continue;
        if (now < mem->frames[i].last_used_ms ||           // Wraparound check
            now - mem->frames[i].last_used_ms < min_age_ms) continue;
        if (mem->frames[i].last_used_ms < oldest) {        // Menor timestamp = más viejo
            oldest = mem->frames[i].last_used_ms;
            victim = i;
        }
    }
}
```

### Parámetros Clave

| Parámetro | Valor | Fuente |
|-----------|-------|--------|
| `DSM_EVICT_MIN_AGE_MS` | **50 ms** | coherence.c:46 |
| Reintentos en `coherence_make_room` | **64 intentos** | coherence.c:812 |
| Umbral de edad mínima | 50 ms (primera pasada), 0 ms (segunda) | coherence.c:675-678 |

### Análisis de la Política

| Característica | Implementación | Problema |
|----------------|----------------|----------|
| Algoritmo | LRU por timestamp `last_used_ms` | Aproximado, no LRU estricto |
| Granularidad temporal | Milisegundos (`dsm_now_ms()`) | ✅ Suficiente |
| Edad mínima | **50 ms hardcodeado** | ⚠️ **Muy agresivo** — expulsa páginas usadas hace 50ms |
| Exclusión | `avoid_page` (página que se está instalando) | ✅ Correcto |
| Escaneo | Lineal O(frame_count) = O(256) | ✅ Aceptable |
| Segunda pasada | `min_age_ms = 0` si no hay víctima | ✅ Evita deadlock |

**Hallazgo Crítico**: `DSM_EVICT_MIN_AGE_MS = 50` ms es **extremadamente bajo**. Una página accedida hace 51 ms es candidata a expulsión. Esto causa **thrashing** bajo cualquier carga con working set > 256 páginas.

---

## 6. Agotamiento de Marcos y Thrashing

### Comportamiento al Llenar Marcos

1. `dsm_memory_alloc_frame` retorna `DSM_INVALID_FRAME` (memory.c:521)
2. Caller (`local_install`, `home_ensure_data`, `dsm_coherence_handle_fault`) llama `coherence_make_room`
3. `coherence_make_room` (coherence.c:794-842):
   - Lee `resident`, `free_frames` via `dsm_memory_usage`
   - Bucle `while (free_frames == 0 && tries < 64)`
   - Llama `coherence_evict_one` con `avoid_page`
   - Si `DSM_ERR_AGAIN` / `PEER_DOWN` / `TIMEOUT` / `NET` → **salta víctima, prueba otra**
   - Si otro error → **aborta**
   - Máximo 64 intentos

### `coherence_evict_one` (coherence.c:665-792)

```c
// Selección víctima
victim = dsm_memory_pick_victim(co->mem, DSM_EVICT_MIN_AGE_MS, avoid_page);
if (victim == DSM_INVALID_PAGE_ID) {
    victim = dsm_memory_pick_victim(co->mem, 0u, avoid_page);  // 2da pasada sin edad mínima
}
if (victim == DSM_INVALID_PAGE_ID) return DSM_ERR_NO_SPACE;

// Verifica residente
if (p->meta.residency != DSM_RES_RESIDENT) return DSM_ERR_AGAIN;

// Si dirty: copiar datos
if (dirty) { buf = malloc(page_size); dsm_memory_copy_out(...); }

// Home: swap_out | Cliente: release_to_home (RPC)
if (co->node_id == co->home_node) {
    if (!swap_enabled || swap_out != OK) { rollback; return DSM_ERR_NO_SPACE; }
    stats_add(SWAP_BYTES, page_size);
} else {
    st = release_to_home(...);  // RPC con timeout
    if (st != OK) { rollback; return st; }
}

// Libera marco, actualiza metadatos, broadcast cond
dsm_memory_release_frame(...);
stats_inc(EVICTIONS);
```

### Thrashing: Causa Raíz (TASK 1M)

| Factor | Valor | Impacto |
|--------|-------|---------|
| **Memoria física/nodo** | 1 MB (256 frames) | **Muy pequeño** para working sets reales |
| **Ventana virtual** | 256 MB (65,536 páginas) | Overcommit 256:1 |
| **Edad mínima expulsión** | 50 ms | Expulsa páginas "recientemente usadas" |
| **Política** | LRU aprox. sin working set detection | No protege páginas frecuentes |
| **Swap** | Habilitado (256 MB archivo) | Añade latencia I/O en expulsión home |
| **Reintentos make_room** | 64 | Bucle largo bajo presión |

**Conclusión TASK 1M**: El thrashing es **inevitable** con esta configuración. TASK 1M probablemente accede a >256 páginas distintas en <50 ms, causando expulsión inmediata de páginas que se volverán a necesitar. La combinación de:
- `node_memory = 1 MB` (demasiado pequeño)
- `DSM_EVICT_MIN_AGE_MS = 50` (demasiado agresivo)
- Sin detección de working set
- Overcommit 256:1

...hace que el sistema pase más tiempo expulsando y trayendo páginas que ejecutando trabajo útil.

---

## 7. Métricas: Cómo Se Miden Realmente

| Métrica | Campo Stats | Fuente de Datos | Actualización |
|---------|-------------|-----------------|---------------|
| **resident** | `DSM_STAT_PAGES_RESIDENT` | `dsm_memory_usage()` → `frame_count - free_top` | `dsm_runtime_refresh_stats()` (bajo demanda) |
| **free** | `DSM_STAT_FRAMES_FREE` | `dsm_memory_usage()` → `free_top` | `dsm_runtime_refresh_stats()` (bajo demanda) |
| **evictions** | `DSM_STAT_EVICTIONS` | `atomic_fetch_add` en `coherence_evict_one:787` | En cada expulsión exitosa |
| **evictions_dirty** | `DSM_STAT_EVICTIONS_DIRTY` | `atomic_fetch_add` en `coherence_evict_one:789` | Solo si `dirty == 1` |
| **swap_bytes** | `DSM_STAT_SWAP_BYTES` | `atomic_fetch_add` en `dsm_memory_swap_out:708` | **Acumulativo histórico** |
| **swap_bytes (refresco)** | `DSM_STAT_SWAP_BYTES` | `atomic_load(&rt->mem.swap_bytes)` en `dsm_runtime_refresh_stats:287` | Sobrescribe con valor atómico al imprimir |

### `dsm_memory_usage` (memory.c:560-579)

```c
void dsm_memory_usage(dsm_memory_t *mem, uint64_t *resident, uint64_t *free_frames)
{
    if (dsm_mutex_lock(&mem->frame_lock) == DSM_OK) {
        libres = mem->free_top;  // Índice tope de pila libre
        dsm_mutex_unlock(&mem->frame_lock);
    }
    *free_frames = libres;
    *resident = mem->frame_count - libres;  // frames totales - libres
}
```

- **Resident** = marcos marcados `USED` (no distingue páginas válidas vs en transición).
- **Free** = `free_top` (entradas en pila de marcos libres).
- **No cuenta** páginas en estado `FETCHING`/`EVICTING` que ya tienen marco asignado.

### Inconsistencia en `swap_bytes`

```c
// memory.c:708: incrementa en swap_out
atomic_fetch_add_explicit(&mem->swap_bytes, (uint64_t)mem->page_size, memory_order_relaxed);

// runtime.c:286-287: refresco para stats
dsm_stats_set(&rt->stats, DSM_STAT_SWAP_BYTES, atomic_load(&rt->mem.swap_bytes));
```

- `swap_bytes` **nunca decrementa** en `swap_in`.
- Reporta **total de bytes escritos en swap desde arranque**, no bytes actualmente en swap.
- Para saber uso real de swap: `evictions_dirty * page_size - swap_ins * page_size` (no trackeado).

---

## 8. Inconsistencias entre Configs y Código

| # | Config / Default | Código | Discrepancia | Severidad |
|---|------------------|--------|--------------|-----------|
| 1 | `node_memory = 1048576` (1 MB) | `frame_count = node_memory / page_size = 256` | **Muy bajo** para cargas reales; validation mínima 16 frames (config.c:356) | 🔴 Crítica |
| 2 | `window_size = 268435456` (256 MB) | Swap file = `window_size` (256 MB) | Swap 256x mayor que memoria física residente | 🟡 Media |
| 3 | `eviction_policy = 0` (LRU) | Hardcodeado `DSM_EVICT_MIN_AGE_MS = 50` en coherence.c:46 | Política no configurable; edad mínima fija | 🔴 Crítica |
| 4 | `fault_queue_size = 1024` | `next_pow2(1024) = 1024` (fault.c:306) | OK, pero queue puede llenar bajo thrashing | 🟢 Baja |
| 5 | `max_message_size = 8192` | `page_size = 4096` + headers | Cabe 1 página + overhead, pero fragmentación posible | 🟡 Media |
| 6 | `thread_pool_size = 32` | Resolver es **single-thread** (runtime.c:193) | Pool usado solo para red, no para resolución fallos | 🟡 Media |
| 7 | `stats_interval_ms = 5000` | `dsm_runtime_refresh_stats` solo se llama en `print_stats` | Métricas **no se actualizan periódicamente** | 🟡 Media |
| 8 | `config_version = 1` | `_Static_assert sizeof(dsm_config_t) == 728` | Estructura fija; añadir campos rompe ABI | 🟡 Media |
| 9 | `bootstrap_timeout_ms = 15000` | `coherence_make_room` tries=64 sin timeout global | Bajo thrashing, 64 expulsiones pueden tardar >15s | 🟡 Media |

### Validación de Config vs Realidad (config.c:333-432)

```c
// config.c:356: Mínimo 16 marcos
if (cfg->node_memory / cfg->page_size < 16u) return DSM_ERR_INVAL;
// Con page_size=4096 → node_memory mínimo = 65536 (64 KB)
// Default 1 MB pasa holgadamente, pero es insuficiente

// config.c:359: window_size ≥ node_memory
if (cfg->window_size < cfg->node_memory) return DSM_ERR_INVAL;
// Default 256 MB ≥ 1 MB ✅

// config.c:387-389: max_message_size
if (cfg->max_message_size < cfg->page_size + 256u ||
    cfg->max_message_size > 64u * 1024u * 1024u) return DSM_ERR_INVAL;
// Default 8192 ≥ 4096+256=4352 ✅
```

---

## 9. Resumen de Hallazgos Críticos

### 🔴 Críticos (Bloquean correctitud/rendimiento)

1. **`node_memory = 1 MB` insuficiente** — 256 frames no sostienen working sets reales. TASK 1M thrashea por diseño.
2. **`DSM_EVICT_MIN_AGE_MS = 50` hardcodeado** — Expulsa páginas a los 50 ms. Debería ser configurable y ≥ 1000 ms.
3. **`swap_bytes` es acumulativo, no actual** — Métrica engañosa para monitoreo.
4. **`coherence_make_room` sin timeout global** — 64 expulsiones secuenciales pueden bloquear resolver indefinidamente.
5. **No hay protección de working set** — LRU puro expulsa páginas frecuentes si working set > frames.

### 🟡 Medianos (Riesgo operativo)

6. **Swap file 256 MB vs 1 MB físico** — Archivo disperso OK en ext4/xfs, problema en FS sin sparse support.
7. **`frame_count` sin validación de overflow en `calloc`** — Seguro con valores actuales, frágil ante config futuro.
8. **Métricas solo bajo demanda** — `stats_interval_ms` ignorado; no hay hilo de refresco periódico.
9. **Single-threaded resolver** — `thread_pool_size = 32` no aplica a resolución de fallos.

### 🟢 Menores (Calidad de código)

10. **`dsm_memory_page_get_or_create` race window** — Lock soltado para `calloc` (manejado con re-check).
11. **`dsm_memory_alloc_frame` falla rápido sin backoff** — Caller reintenta en bucle tight (coherence.c:456-460).
12. **`backing_page_addr` duplicado logic** — Igual que `dsm_memory_page_addr` pero con `backing_base`.

---

## 10. Recomendaciones Prioritarias

| Prioridad | Acción | Archivos Afectados |
|-----------|--------|-------------------|
| **P0** | Subir `node_memory` a ≥ 64 MB (16K frames) o hacerlo configurable por app | `config/node*.conf`, `config.c:356` |
| **P0** | Hacer `DSM_EVICT_MIN_AGE_MS` configurable (mín 1000 ms) | `coherence.c:46`, `dsm_config.h`, `config.c` |
| **P0** | Cambiar `swap_bytes` a contador de páginas en swap (inc en swap_out, dec en swap_in) | `memory.c:708, 731`, `coherence.c:770, 611` |
| **P1** | Añadir timeout global a `coherence_make_room` (usar `shutdown_timeout_ms` o nuevo config) | `coherence.c:794-842` |
| **P1** | Implementar working set detection (contador de accesos por página, proteger top-N) | `memory.c`, `coherence.c` |
| **P1** | Hilo periódico de refresco stats (usar `stats_interval_ms`) | `runtime.c`, `dsm_runtime.h` |
| **P2** | Validar overflow en `calloc(frame_count, sizeof(dsm_frame_t))` | `memory.c:160` |
| **P2** | Unificar `backing_page_addr` y `dsm_memory_page_addr` | `memory.c:279, 431` |

---

## Evidencia de Código Citada

| Referencia | Archivo:Línea | Descripción |
|------------|---------------|-------------|
| page_count calc | memory.c:119 | `mem->page_count = window_size / page_size` |
| frame_count calc | runtime.c:131 | `mp.frame_count = node_memory / page_size` |
| validación page_size | config.c:349-351 | Potencia de 2, ≥ 4096 |
| validación frames mín | config.c:356-358 | `node_memory / page_size ≥ 16` |
| swap_open | memory.c:664 | `ftruncate(fd, window_size)` |
| swap_out | memory.c:703-709 | `pwrite` + `atomic_fetch_add(swap_bytes)` |
| swap_in | memory.c:726-730 | `pread` (sin decremento swap_bytes) |
| pick_victim | memory.c:603-645 | LRU con `min_age_ms` |
| EVICT_MIN_AGE_MS | coherence.c:46 | `#define DSM_EVICT_MIN_AGE_MS 50u` |
| coherence_make_room | coherence.c:794-842 | Bucle 64 intentos, salta errores red |
| coherence_evict_one | coherence.c:665-792 | Expulsión con swap_out/release_to_home |
| memory_usage | memory.c:560-579 | `resident = frame_count - free_top` |
| runtime_refresh_stats | runtime.c:273-288 | Actualiza resident, free, swap_bytes |
| swap_bytes refresh | runtime.c:286-287 | `atomic_load(&rt->mem.swap_bytes)` |

---

**Fin del Informe de Auditoría**  
*Generado por AGENTE 3 — Solo lectura, sin modificaciones al código.*