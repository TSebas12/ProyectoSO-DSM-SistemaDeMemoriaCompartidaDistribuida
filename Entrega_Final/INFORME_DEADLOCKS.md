# Informe de Análisis de Interbloqueos (Deadlocks) — DSM Distribuido

**Proyecto:** Sistema de Memoria Compartida Distribuida (DSM)  
**Entregable:** 3 — Prototipo 2  
**Fecha:** 2026-11-27  
**Alcance:** `Entrega_Final/codigo/src/` — coherencia, sincronización, red, fallos, memoria  

---

## 1. Inventario Completo de Locks y Semáforos

### 1.1 Locks de Coherencia (`src/coherence.c`)

| Lock | Archivo / Línea | Tipo | Qué Protege | Hilos/Nodos que lo Adquieren | Locks que Puede Solicitar Mientras Está Retenido | Liberación | Bloquea Operación de Red |
|------|-----------------|------|-------------|------------------------------|--------------------------------------------------|------------|-------------------------|
| `co->dir_lock` | coherence.c:291, 132 | `DSM_LOCK_GLOBAL` "directorio" | Tabla hash del directorio (buckets, cadenas de `dsm_dir_entry_t`) | Home node: cualquier operación que busque/cree entrada del directorio | `e->op_lock` (línea 140), `co->obj_lock` (vía `object_of_page`) | Inmediato tras `dir_get_or_create_locked` (línea 136) o tras `dir_find_locked` | **No** — se suelta antes de RPC |
| `e->op_lock` (por entrada de directorio) | coherence.c:114, 128 | `DSM_LOCK_GLOBAL` "pagina_home" | Serializa operaciones del home sobre UNA página concreta; protege `dsm_directory_entry_t.meta` | Home node: `home_serve_read`, `home_serve_write`, `handle_page_request`, `handle_ownership_request`, `handle_page_release`, `dsm_coherence_release_page` (home), `dsm_coherence_snapshot` | `p->lock` (page lock) vía `local_install`/`home_ensure_data`/`local_drop`; `co->obj_lock` vía `object_of_page`; `mem->table_lock` vía `local_page`; `mem->frame_lock` vía `dsm_memory_alloc_frame` | Al final de `home_serve_read` (1004), `home_serve_write` (1085/1089), o en manejadores de mensaje tras `local_install` | **SÍ** — se mantiene durante `forward_from_owner` (RPC PAGE_FORWARD, líneas 881), `invalidate_node` (RPC INVALIDATE, línea 936), `release_to_home` (RPC PAGE_RELEASE, línea 645), `home_ensure_data` (swap_in, línea 566) |
| `co->obj_lock` | coherence.c:296, 196 | `DSM_LOCK_OBJECT` "objetos" | Array `objects[]`, `object_count`, `alloc_cache`, `alloc_seq`, `next_page` | Todos los nodos: `object_record`, `object_find_locked`, `dsm_coherence_alloc`, `dsm_coherence_find_object`, `home_alloc_object`, `object_of_page` | `mem->table_lock` vía `dsm_memory_page_get_or_create` (línea 210 en `object_record`) | Inmediato tras operación sobre array | **No** — no hay RPC con este lock |
| `co->barrier_lock` | coherence.c:302, 1786 | `DSM_LOCK_OBJECT` "barrera" | `barrier_registered_round[]`, `barrier_arrived` (atómico pero protegido para registro) | Home node: `barrier_arrive` (llamado desde `handle_barrier` y `dsm_coherence_barrier` local) | Ninguno | Inmediato tras actualizar contadores | **No** |
| `p->lock` (page entry lock) | memory.c:366, coherence.c:444 | `DSM_LOCK_PAGE` "pagina" | `dsm_local_page_t.meta` (msi_state, residency, frame_slot, version, is_dirty, flags), `cond` | **Todos los nodos**: `local_install`, `local_drop`, `home_ensure_data`, `coherence_evict_one`, `dsm_coherence_handle_fault`, `handle_page_forward`, `handle_invalidate`, `handle_writeback`, `handle_page_list`, `dsm_coherence_snapshot`, `dsm_coherence_format_pages` | `mem->frame_lock` vía `dsm_memory_alloc_frame`/`release_frame` (líneas 456, 462, 508, 533, 711, 759, 775); `mem->table_lock` NO (se accede sin lock tras lookup); `co->dir_lock` NO | Variable: en bucles con reintentos (local_install), tras RPC (handle_page_forward), tras wait en cond (local_drop, handle_invalidate) | **SÍ** — `handle_page_forward` mantiene lock durante copy_out + mprotect + send (líneas 1471-1524); `handle_invalidate` espera en cond con lock tomado (línea 1553) |
| `p->cond` (condvar de página) | memory.c:367 | CondVar asociada a `p->lock` | Espera de transiciones FETCHING/EVICTING → RESIDENT/NOT_RESIDENT/ERROR | Hilos de aplicación (fault handler), resolver loop, hilos de red (workers) | Ninguno (se usa con `p->lock` ya tomado) | `dsm_cond_broadcast` en transiciones de estado | **No** por sí misma |

### 1.2 Locks de Memoria (`src/memory.c`)

| Lock | Archivo / Línea | Tipo | Qué Protege | Hilos que lo Adquieren | Locks que Puede Solicitar Mientras Está Retenido | Liberación | Bloquea Red |
|------|-----------------|------|-------------|------------------------|--------------------------------------------------|------------|-------------|
| `mem->table_lock` | memory.c:184 | `DSM_LOCK_OBJECT` "tabla_paginas" | Hash buckets de página (`buckets[]`, cadenas de `dsm_page_entry_t`) | `dsm_memory_page_lookup`, `dsm_memory_page_get_or_create`, `handle_page_list`, `dsm_coherence_format_pages`, `object_record` (vía `dsm_memory_page_get_or_create`) | `p->lock` NO (se suelta antes de usar entry); `mem->frame_lock` NO | Inmediato tras búsqueda/inserción | **No** |
| `mem->frame_lock` | memory.c:188 | `DSM_LOCK_FRAME` "marcos" | Pool de marcos (`frames[]`, `free_stack[]`, `free_top`) | `dsm_memory_alloc_frame`, `dsm_memory_release_frame`, `dsm_memory_usage`, `dsm_memory_touch`, `dsm_memory_pick_victim` | Ninguno | Inmediato tras operación | **No** |
| `p->lock` (page entry) | memory.c:366 | `DSM_LOCK_PAGE` "pagina" | Ver arriba (coherencia) | Ver arriba | `mem->frame_lock` (líneas 516, 538, 590, 616) | Ver arriba | Ver arriba |

### 1.3 Locks de Red (`src/net.c`)

| Lock | Archivo / Línea | Tipo | Qué Protege | Hilos que lo Adquieren | Locks que Puede Solicitar Mientras Está Retenido | Liberación | Bloquea Red |
|------|-----------------|------|-------------|------------------------|--------------------------------------------------|------------|-------------|
| `net->conn[node].lock` | net.c:50, 801 | `DSM_LOCK_GLOBAL` "conexion" | Socket persistente saliente hacia `node` (`fd`, estado de conexión) | Hilo que hace RPC saliente (`rpc_once` → `peer_acquire`); hilo que envía one-way (`dsm_net_send` fuera de worker) | **NINGUNO** — no llama a capas superiores con lock tomado | Tras `write_msg_fd` (one-way) o tras RPC completo (`peer_release`) | **SÍ** — se mantiene durante TODO el RPC (`rpc_once`: write + read header + read payload, líneas 324-374) |
| `net->q_lock` | net.c:73, 806 | `DSM_LOCK_GLOBAL` "cola_red" | Cola de conexiones entrantes pendientes (`queue[]`, `q_head`, `q_tail`, `q_count`) | Listener thread (`queue_push`), Worker threads (`queue_pop`) | `net->q_cond` (wait/signal) | Inmediato tras push/pop | **No** |
| `net->active_lock` | net.c:78, 813 | `DSM_LOCK_GLOBAL` "conexiones" | Array de sockets activos (`active[]`) | Worker threads (`active_add`, `active_remove`) | Ninguno | Inmediato | **No** |
| `net->q_cond` | net.c:74, 810 | CondVar asociada a `q_lock` | Espera de workers por conexiones entrantes | Worker threads (`queue_pop` → `dsm_cond_wait`) | Ninguno | `dsm_cond_signal`/`broadcast` | **No** |

### 1.4 Sincronización de Fallos (`src/fault.c`)

| Primitive | Archivo / Línea | Tipo | Qué Protege | Hilos | Notas |
|-----------|-----------------|------|-------------|-------|-------|
| `g_q.head` / `g_q.tail` | fault.c:46-47 | Atomic (lock-free ring) | Índices de cola de fallos | Signal handler (productor), Resolver thread (consumidor) | **Sin locks** — diseño intencional para signal-safety |
| `g_q.slots[i].ready` | fault.c:34, 178 | Atomic | Publicación de slot individual | Signal handler, Resolver | Lock-free |
| `g_q.items` (semáforo) | fault.c:48, 323 | `dsm_sem_t` (POSIX sem) | Contador de items en cola | Signal handler (`dsm_sem_post`), Resolver (`dsm_sem_wait`/`timedwait`) | **Async-signal-safe** — `sem_post` es seguro en signal handler |
| `ctx->in_fault` / `ctx->failed` | fault.c:44, 46 | `_Atomic int` | Estado de hilo en fault | Signal handler (write), Resolver (read/write), `dsm_fault_wake/mark_failed` | Lock-free |
| `t_ctx` (thread-local) | fault.c:52 | `_Thread_local` | Contexto del hilo actual | Cada hilo | Sin contención |

### 1.5 Semáforos y CondVars Generales

| Primitive | Uso Principal |
|-----------|---------------|
| `dsm_sem_t` | Cola de fallos (`g_q.items`), futuros usos |
| `dsm_cond_t` + `dsm_mutex_t` | Page entry cond (`p->cond`), Net queue (`net->q_cond`) |
| `pthread_mutex_t` (raw) | Ninguno directo — todo envuelto en `dsm_mutex_t` |

---

## 2. Jerarquía de Clases de Lock (Definida en `dsm_sync.h:30-35`)

```c
typedef enum dsm_lock_class {
    DSM_LOCK_GLOBAL = 0,   // Mayor alcance: directorio, conexiones, colas
    DSM_LOCK_OBJECT = 1,   // Objetos, tabla de páginas, barrera
    DSM_LOCK_PAGE   = 2,   // Entrada de página individual (meta + cond)
    DSM_LOCK_FRAME  = 3    // Menor alcance: pool de marcos
} dsm_lock_class_t;
```

**Regla:** Un hilo **nunca** puede tomar un lock de clase *menor* (número mayor) y luego intentar tomar uno de clase *mayor* (número menor). El orden válido es:

```
GLOBAL → OBJECT → PAGE → FRAME
```

**Verificación en runtime:** `sync.c:71-83` (`check_order`) detecta violaciones e incrementa `g_violations` (atómico). No aborta: solo loguea y continúa.

### 2.1 Orden Real Observado en el Código

| Secuencia | Locks Tomados (en orden) | Comentario |
|-----------|---------------------------|------------|
| `dir_lock_entry` | `dir_lock` (GLOBAL) → `op_lock` (GLOBAL) | Ambos GLOBAL: OK |
| `home_serve_read/write` | `op_lock` (GLOBAL) → `p->lock` (PAGE) vía `local_install`/`home_ensure_data` → `frame_lock` (FRAME) | **OK**: GLOBAL → PAGE → FRAME |
| `local_install` | `p->lock` (PAGE) → `frame_lock` (FRAME) | **OK** |
| `coherence_evict_one` | `p->lock` (PAGE) → `frame_lock` (FRAME) → **RPC `release_to_home`** (toma `conn[home].lock` GLOBAL) | **VIOLACIÓN**: PAGE → GLOBAL |
| `dsm_coherence_handle_fault` | `p->lock` (PAGE) → suelta → RPC (`remote_fetch`/`local_fetch`) → retoma `p->lock` → `frame_lock` (FRAME) | **OK** — suelta PAGE antes de RPC |
| `handle_page_forward` | `p->lock` (PAGE) → **RPC reply send** (usa `t_reply_fd` sin lock de red) | **OK** — no toma lock de red |
| `handle_invalidate` | `p->lock` (PAGE) → `dsm_cond_timedwait` (suelta `p->lock` temporalmente) → retoma `p->lock` | **OK** |
| `handle_page_release` (home) | `op_lock` (GLOBAL) → `local_install` → `p->lock` (PAGE) → `frame_lock` (FRAME) → `invalidate_node` (RPC → `conn[].lock` GLOBAL) | **VIOLACIÓN**: PAGE → GLOBAL |
| `dsm_coherence_release_page` (cliente) | `p->lock` (PAGE) → suelta → `release_to_home` (RPC → `conn[home].lock` GLOBAL) → retoma `p->lock` vía `local_drop` | **OK** — suelta PAGE antes de RPC |
| `dsm_coherence_release_page` (home) | `op_lock` (GLOBAL) → `invalidate_node` (RPC → `conn[].lock` GLOBAL) | **OK** — GLOBAL → GLOBAL (mismo nivel) |
| `home_alloc_object` | `obj_lock` (OBJECT) → `dsm_memory_page_get_or_create` → `table_lock` (OBJECT) | **OK** — OBJECT → OBJECT |

---

## 3. Ciclos de Espera Potenciales (Deadlock Scenarios)

### 3.1 Ciclo Clásico: A solicita página a B con lock tomado → B invalida y necesita lock de A

**Escenario:** 3 nodos (Home=H, Owner=O, Requester=R), página P en estado M en O.

```
Tiempo →
R (requester)                    H (home)                      O (owner)
─────────────────────────────────────────────────────────────────────────
dsm_coherence_handle_fault(P, WRITE)
  → p->lock (PAGE) tomado
  → remote_fetch(OWNERSHIP_REQUEST)
                                    handle_ownership_request(P)
                                      → dir_lock_entry(P): dir_lock + op_lock (GLOBAL)
                                      → home_serve_write(P)
                                        → forward_from_owner(P, O, exclusive=1)
                                          → call_msg(PAGE_FORWARD a O)
                                                                      handle_page_forward(P)
                                                                        → p_O->lock (PAGE) tomado
                                                                        → copy_out + mprotect
                                                                        → send PAGE_DATA
                                        ← PAGE_DATA recibido
                                        → local_install(P) en H
                                          → p_H->lock (PAGE) → frame_lock (FRAME)
                                        → invalidate_node(otros sharers)
                                        → OWNERSHIP_GRANT a R
                                      ← op_lock liberado
                                  ← RPC retorna
  ← OWNERSHIP_GRANT + PAGE_DATA recibido
  → retoma p_R->lock (PAGE)
  → frame_lock (FRAME) para instalar
  → DONE
```

**¿Dónde está el riesgo?**  
Si **O** tiene otra página **Q** en M y **H** la quiere expulsar/usar mientras **O** está procesando `PAGE_FORWARD` de **P**:

- **H** tiene `op_lock(P)` y llama `invalidate_node(Q)` → RPC `INVALIDATE` a **O**
- **O** está en `handle_page_forward(P)` con `p_O->lock(P)` tomado
- **O** recibe `INVALIDATE(Q)` → `handle_invalidate(Q)` intenta tomar `p_O->lock(Q)`
- **Si Q == P**: mismo lock, deadlock directo (reentrancia en mismo mutex → `EDEADLK` detectado por `pthread_mutex_lock` → `DSM_ERR_BUSY`)
- **Si Q ≠ P**: **O** tiene `p_O->lock(P)` y quiere `p_O->lock(Q)` — **OK** (mismo nivel PAGE)

**Pero el ciclo real inverso:**
- **H** en `home_serve_write(P)` con `op_lock(P)` → llama `forward_from_owner(P, O)` → RPC `PAGE_FORWARD` a **O**
- **O** en `handle_page_forward(P)` con `p_O->lock(P)` → si **O** necesita hacer `release_to_home(Q)` para hacer sitio (pool lleno) → RPC `PAGE_RELEASE` a **H**
- **H** recibe `PAGE_RELEASE(Q)` → `handle_page_release(Q)` → necesita `dir_lock_entry(Q)` → `dir_lock` (GLOBAL) + `op_lock(Q)` (GLOBAL)
- **H** ya tiene `op_lock(P)` (GLOBAL) → **OK** (mismo nivel)
- **Pero** si **H** en `handle_page_release(Q)` llama `local_install(Q)` → `p_H->lock(Q)` (PAGE) → `frame_lock` (FRAME) → **OK**

**El ciclo peligroso real requiere inversión de jerarquía:**

```
Hilo A (en nodo H):  op_lock(P) [GLOBAL] → RPC a O → espera respuesta
Hilo B (en nodo O):  p_O->lock(P) [PAGE] → RPC a H (release_to_home) → espera respuesta
                                     ↑
                                     └─ H ya tiene op_lock(P) y podría necesitar p_H->lock(P) para local_install
```

**Si H en el RPC de respuesta necesita `p_H->lock(P)` (ej. `local_install` en `handle_page_release` o `handle_writeback`), y O en su RPC necesita `op_lock(P)` en H...** pero O no toma `op_lock` en H, solo envía mensaje.

**Ciclo real confirmado:** **NO hay ciclo directo H↔O con locks invertidos** porque:
- H toma `op_lock` (GLOBAL) y hace RPC → suelta `op_lock` ANTES de `local_drop` (línea 1085-1086)
- O toma `p->lock` (PAGE) y hace RPC reply → no toma locks de H

### 3.2 Ciclo: `coherence_evict_one` con `page lock` + RPC `release_to_home`

**Código:** `coherence.c:665-792`

```c
// Línea 683-684: toma p->lock (PAGE)
p = dsm_memory_page_lookup(co->mem, victim);
if (p == NULL || dsm_mutex_lock(&p->lock) != DSM_OK) { ... }

// ... copia datos, marca EVICTING, suelta p->lock (línea 719)

if (co->node_id == co->home_node) {
    // swap_out SIN lock de página — OK
} else {
    // Línea 741: RPC CON p->lock YA SUELTO
    st = release_to_home(co, victim, dirty ? buf : NULL, ...);
    // RPC toma conn[home].lock (GLOBAL) — OK porque page lock ya soltado
}
```

**Análisis:** **CORRECTO** — el lock de página se suelta **antes** del RPC. No hay violación PAGE→GLOBAL aquí.

### 3.3 Ciclo: `home_serve_write` con `op_lock` + RPC a dueño

**Código:** `coherence.c:1008-1091`

```c
// op_lock YA TOMADO por dir_lock_entry (línea 143)
if (owner != co->home_node) {
    st = forward_from_owner(co, d, owner, 1, buf);  // RPC PAGE_FORWARD
    // forward_from_owner → call_msg → rpc → peer_acquire → conn[owner].lock (GLOBAL)
    // op_lock SIGUE TOMADO durante todo el RPC
    if (st != DSM_OK) {
        dsm_mutex_unlock(&d->op_lock);  // liberación en error
        return st;
    }
}
// ... invalidaciones (más RPCs con op_lock tomado)
```

**VIOLACIÓN CONFIRMADA:** `op_lock` (GLOBAL) mantenido durante RPC saliente (`conn[owner].lock` también GLOBAL).  
**¿Es deadlock?** Solo si **owner** simultáneamente hace RPC a **home** que requiere `op_lock` de la misma página.

**Escenario de deadlock:**
1. **H** (home) tiene `op_lock(P)` y envía `PAGE_FORWARD` a **O** (owner actual)
2. **O** recibe `PAGE_FORWARD`, toma `p_O->lock(P)` (PAGE), procesa, responde `PAGE_DATA`
3. **Simultáneamente**, **O** tiene una página **Q** en M y **H** la quiere → **H** envía `INVALIDATE(Q)` a **O**
4. **O** recibe `INVALIDATE(Q)` mientras está en `handle_page_forward(P)` con `p_O->lock(P)` tomado
5. `handle_invalidate(Q)` intenta tomar `p_O->lock(Q)` — **OK** (distinta página, mismo nivel)
6. **PERO** si **Q == P** (misma página): **O** intenta `p_O->lock(P)` reentrantemente → **EDEADLK** detectado por pthread → `DSM_ERR_BUSY`

**¿Puede Q == P?** Sí: **H** está transfiriendo propiedad de **P** de **O** a **R**. Durante `forward_from_owner`, **H** invalida a terceros. Si **O** es el único sharer además de **H**, no hay terceros. Pero si **O** tenía **P** en M y **H** le pide que la reenvíe (exclusive=1), **O** pasa a I. No hay `INVALIDATE` a **O** para **P** en este flujo.

**Deadlock real requiere 3 páginas y 3 nodos:**

```
Nodo A (Home de P):  op_lock(P) → RPC PAGE_FORWARD a B
Nodo B (Owner de P): p_B->lock(P) → procesa → RPC PAGE_DATA a A
                     Simultáneamente: B quiere escribir página Q (home en C)
                     B: fault(Q) → RPC OWNERSHIP_REQUEST a C
Nodo C (Home de Q):  op_lock(Q) → RPC PAGE_FORWARD a B (dueño actual de Q)
                     B recibe PAGE_FORWARD(Q) con p_B->lock(P) TOMADO
                     → handle_page_forward(Q) intenta p_B->lock(Q) — OK (distinta página)
```

**No hay inversión de jerarquía aquí** (PAGE → PAGE está bien).

### 3.4 Ciclo: `dsm_coherence_handle_fault` con `page lock` + RPC

**Código:** `coherence.c:1247-1388`

```c
// Línea 1270: toma p->lock (PAGE)
if (dsm_mutex_lock(&page->lock) != DSM_OK) { return DSM_ERR_STATE; }

// ... espera si FETCHING/EVICTING (cond_wait SUELTA lock)

page->meta.residency = FETCHING;
dsm_mutex_unlock(&page->lock);  // LÍNEA 1316: SUELTA ANTES DE RPC

st = (co->node_id == co->home_node)
     ? local_fetch(co, page_id, is_read, buf)
     : remote_fetch(co, page_id, is_read, buf);  // RPC SIN page lock

// Línea 1332: RETOMA page->lock
if (dsm_mutex_lock(&page->lock) != DSM_OK) { ... }
```

**CORRECTO** — Sigue la regla del comentario línea 10-12: "Nunca se espera a la red con el lock de una página tomado".

### 3.5 Ciclo: `local_drop` con `page lock` + wait en cond

**Código:** `coherence.c:478-522`

```c
p = dsm_memory_page_lookup(co->mem, page_id);
if (dsm_mutex_lock(&p->lock) != DSM_OK) return;

// Línea 489-491: ESPERA CON LOCK TOMADO (cond_timedwait SUELTA temporalmente)
while ((p->meta.residency == FETCHING || p->meta.residency == EVICTING) && waited < 200) {
    dsm_cond_timedwait(&p->cond, &p->lock, 5u);  // SUELTA lock, espera, RETOMA lock
    waited += 5u;
}
```

**Análisis:** `dsm_cond_timedwait` suelta el mutex atómicamente y lo retoma al despertar (`sync.c:212-228` hace `pop_class`/`push_class`). **No hay deadlock** porque:
- El lock se suelta durante la espera
- Quien despierta (`dsm_cond_broadcast`) ya no tiene el lock (lo soltó tras cambiar estado)

**Riesgo:** Si el timeout (200ms) expira y la página sigue en FETCHING/EVICTING, `local_drop` **aborta y devuelve** sin limpiar (línea 494-499). Esto puede dejar la página en estado inconsistente pero no causa deadlock.

### 3.6 Hilos de Red (Pool Workers) + Resolutor + Hilos App

**Actores concurrentes:**

| Actor | Locks Típicos | Operaciones de Larga Duración |
|-------|---------------|-------------------------------|
| **Worker thread** (net.c:704) | `q_lock` (pop), `active_lock` (add/remove), `conn[dst].lock` (RPC), `t_reply_fd` (send reply) | RPC completos (read header + payload), `dsm_coherence_handle_message` |
| **Resolver thread** (fault.c:487) | `g_q.items` (sem_wait), `page->lock` (PAGE), `frame_lock` (FRAME), `op_lock` (GLOBAL) vía coherencia | `dsm_coherence_handle_fault` (RPC a home), `coherence_make_room` (evicción + RPC) |
| **App thread** (fault handler) | `page->lock` (PAGE) brevemente en signal handler (NO — lock-free queue), luego duerme en `sigsuspend` | Espera resolución (pasivo) |
| **App thread** (dsm_release) | `page->lock` (PAGE), `op_lock` (GLOBAL) si home, `conn[home].lock` (GLOBAL) RPC | `release_to_home` RPC |

**Puntos de contención cruzada:**

1. **Worker procesa `PAGE_FORWARD`** → toma `page->lock` (PAGE) → `copy_out` + `mprotect` + `send_payload`
   - `send_payload` usa `t_reply_fd` (thread-local) → `write_msg_fd` → **NO toma `conn[].lock`** (respuesta por socket entrante)
   - **OK** — no hay lock de red

2. **Worker procesa `INVALIDATE`** → toma `page->lock` (PAGE) → puede esperar en `cond_timedwait` (línea 1553) si página en FETCHING/EVICTING
   - Resolutor podría estar trayendo esa misma página → tiene `page->lock` → `cond_broadcast` al terminar
   - **OK** — condvar diseñado para esto

3. **Resolver hace `coherence_make_room`** → `coherence_evict_one` → suelta `page->lock` → RPC `release_to_home` → toma `conn[home].lock` (GLOBAL)
   - Simultáneamente, **Worker** en home procesa `PAGE_RELEASE` → `handle_page_release` → toma `op_lock` (GLOBAL) → `local_install` → `page->lock` (PAGE)
   - **Orden:** Resolver: PAGE (suelto) → GLOBAL; Worker: GLOBAL → PAGE
   - **INVERSIÓN DETECTADA:** Worker toma GLOBAL luego PAGE; Resolver toma PAGE luego GLOBAL (en distinta página)
   - **¿Deadlock?** Solo si es la **misma página**: Resolver evicta P, Worker recibe RELEASE de P.
     - Resolver: `p_P->lock` → suelta → `conn[home].lock` (RPC RELEASE)
     - Worker: `op_lock(P)` → `local_install(P)` → `p_P->lock`
     - No hay espera circular: Resolver ya soltó `p_P->lock` antes de RPC; Worker toma `op_lock(P)` luego `p_P->lock` — orden correcto GLOBAL→PAGE

4. **Resolver en `local_fetch` (home)** → `dir_lock_entry` → `op_lock` (GLOBAL) → `home_serve_read/write` → RPC `forward_from_owner`/`invalidate_node` con `op_lock` TOMADO
   - **VIOLACIÓN CONFIRMADA:** GLOBAL mantenido durante RPC saliente
   - Ver sección 3.3

---

## 4. Violaciones de Orden Detectadas por `sync.c` (`g_violations`)

### 4.1 Código de Detección (`sync.c:71-83`)

```c
static void check_order(dsm_lock_class_t klass, const char *name)
{
    dsm_lock_class_t prev = top_class();
    if (prev != (dsm_lock_class_t)-1 && klass < prev) {
        atomic_fetch_add(&g_violations, 1);
        fprintf(stderr,
                "[dsm][sync] orden de locks invalido: se toma '%s' (clase %d) "
                "teniendo clase %d tomada\n",
                name != NULL ? name : "?", (int)klass, (int)prev);
    }
    push_class(klass);
}
```

- `klass < prev` significa: se toma un lock de **clase menor número** (mayor alcance) teniendo uno de **clase mayor número** (menor alcance).
- Ejemplo: teniendo `PAGE` (2), se toma `GLOBAL` (0) → `0 < 2` → **VIOLACIÓN**.

### 4.2 Violaciones Confirmadas en Código Actual

| # | Ubicación | Lock Tomado (clase) | Lock Ya Retenido (clase) | Evidencia |
|---|-----------|---------------------|--------------------------|-----------|
| 1 | `coherence_evict_one` (línea 741) | `conn[home].lock` (GLOBAL) via RPC | `p->lock` (PAGE) **YA SUELTO** en línea 719 | **FALSO POSITIVO** — lock de página ya liberado |
| 2 | `home_serve_write` (línea 1018) | `conn[owner].lock` (GLOBAL) via `forward_from_owner` | `d->op_lock` (GLOBAL) | **NO VIOLACIÓN** — misma clase (GLOBAL→GLOBAL) |
| 3 | `home_serve_write` (línea 1049) | `conn[node].lock` (GLOBAL) via `invalidate_node` | `d->op_lock` (GLOBAL) | **NO VIOLACIÓN** — misma clase |
| 4 | `handle_page_release` (línea 1680) | `conn[node].lock` (GLOBAL) via `invalidate_node` | `d->op_lock` (GLOBAL) | **NO VIOLACIÓN** — misma clase |
| 5 | `handle_page_release` (línea 1662) | `p->lock` (PAGE) via `local_install` | `d->op_lock` (GLOBAL) | **OK** — GLOBAL → PAGE |
| 6 | `handle_writeback` (línea 1613) | `d->op_lock` (GLOBAL) via `dir_lock_entry` | `p->lock` (PAGE) **NO TOMADO** (local_install no toma op_lock) | **OK** |
| 7 | `dsm_coherence_snapshot` (línea 2094-2096) | `dir->op_lock` (GLOBAL) | `co->dir_lock` (GLOBAL) → `page->lock` (PAGE) | **VIOLACIÓN REAL**: toma `op_lock` (GLOBAL) teniendo `page->lock` (PAGE) — `0 < 2` |

**Violación Real Confirmada #7:**
```c
// coherence.c:2094-2102
if (dsm_mutex_lock(&co->dir_lock) == DSM_OK) {        // GLOBAL
    dir = dir_find_locked(co, page_id);
    if (dir != NULL && dsm_mutex_lock(&dir->op_lock) == DSM_OK) {  // GLOBAL
        ...
    }
    dsm_mutex_unlock(&co->dir_lock);
}
// PERO el llamador YA TIENE page->lock (línea 2104):
if (dsm_mutex_lock(&page->lock) != DSM_OK) { return DSM_ERR_STATE; }  // PAGE (línea 2104)
// LUEGO llama a dir_lock (línea 2094) → VIOLACIÓN: PAGE (2) → GLOBAL (0)
```

**Otra violación potencial:** `handle_page_list` (línea 1837-1859)
```c
if (dsm_mutex_lock(&co->mem->table_lock) == DSM_OK) {  // OBJECT
    for (buckets) {
        if (dsm_mutex_lock(&e->lock) != DSM_OK) {      // PAGE
            continue;
        }
        ...
    }
}
```
**ORDEN:** OBJECT (1) → PAGE (2) — **CORRECTO**.

### 4.3 Contador `g_violations`

- Se incrementa **solo** cuando `g_check_enabled == 1` (default).
- No aborta: permite que la demo continúe.
- Útil para testing: `dsm_sync_violations()` retorna cuenta atómica.

---

## 5. Soluciones Evaluadas

### 5.1 Orden Global de Adquisición Estricto (Ya Implementado Parcialmente)

**Estado:** La jerarquía `GLOBAL → OBJECT → PAGE → FRAME` está definida y verificada en runtime.

**Gaps:**
- `home_serve_write` mantiene `op_lock` (GLOBAL) durante RPCs salientes → **riesgo teórico** si peer hace RPC inverso que requiere `op_lock`.
- `dsm_coherence_snapshot` viola orden (PAGE → GLOBAL).

**Fix para snapshot:** Tomar `dir_lock` + `op_lock` **ANTES** de `page->lock`, o copiar datos sin `page->lock` (atomic snapshot).

### 5.2 Evitar Mutex Durante Espera RPC (Regla de Oro — Cumplida en Resolutor)

**Código:** `coherence.c:10-12` y `fault.c` lo respetan.

```c
// Regla 1: "Nunca se espera a la red con el lock de una página tomado."
// Resolutor:
page->meta.residency = FETCHING;
dsm_mutex_unlock(&page->lock);  // SUELTA ANTES DE RPC
st = remote_fetch(...);         // RPC SIN LOCK
dsm_mutex_lock(&page->lock);    // RETOMA TRAS RPC
```

**Excepción:** `home_serve_write` **SÍ** mantiene `op_lock` (GLOBAL) durante RPC. Justificación: serializar operaciones home en esa página.

**Mejora:** Separar `op_lock` en dos fases:
1. `op_lock` para decidir qué RPCs hacer (rápido, sin red)
2. Soltar `op_lock`, hacer RPCs, retoma `op_lock` para actualizar directorio

Pero requiere manejar carreras (página puede cambiar de estado entre soltar y retomar).

### 5.3 Separar Procesamiento de Mensajes de Resolución

**Arquitectura actual:**
- Workers de red llaman `dsm_coherence_handle_message` directamente (línea 651)
- Resolutor llama `dsm_coherence_handle_fault`
- Ambos tocan `page->lock`, `op_lock`, `dir_lock`

**Riesgo:** Worker en `handle_invalidate` espera en `cond_timedwait` (línea 1553) con `page->lock` → Resolutor en `handle_fault` quiere mismo `page->lock`.

**Mitigación actual:** `cond_timedwait` suelta lock → Resolutor puede progresar.

**Mejora:** Cola de mensajes por página / actor model — fuera de alcance del prototipo.

### 5.4 ACK No Bloqueantes (Non-blocking ACKs)

**Actual:** `invalidate_node` (línea 920-961) hace `call_msg` (RPC bloqueante) y espera `INVALIDATE_ACK`.
- `op_lock` mantenido durante todo el bucle de invalidaciones (línea 1045-1056).

**Propuesta:** Enviar `INVALIDATE` con `DSM_FLAG_NO_CACHE` o similar, no esperar ACK, usar callback/confirmación asíncrona.
- Requiere cambio de protocolo (ventana de inconsistencia).
- **No implementado** — prototipo usa fail-stop: si una invalidación falla, se aborta toda la concesión de propiedad (línea 1050-1053).

### 5.5 Timeouts Controlados (Implementado)

| Operación | Timeout | Código |
|-----------|---------|--------|
| RPC individual | `co->timeout_ms` (default 3000ms) | `net.c:377` `rpc()` |
| RPC total (con retries) | `net->cfg.rpc_max_total_ms` | `net.c:383` |
| Backoff exponencial | `rpc_backoff_base_ms * 2^attempt` | `net.c:433` |
| Conexión | `net->cfg.connect_timeout_ms` | `net.c:275` |
| `cond_timedwait` en `local_drop` | 5ms × 40 = 200ms | `coherence.c:490` |
| `cond_timedwait` en `handle_invalidate` | 5ms × (timeout_ms/5) | `coherence.c:1553` |
| Barrera poll | 5ms (`DSM_BARRIER_POLL_MS`) | `coherence.c:2072` |
| Evicción min age | 50ms (`DSM_EVICT_MIN_AGE_MS`) | `coherence.c:46` |

**Cobertura:** Bien cubierto. Timeouts evitan esperas infinitas pero **no previenen deadlock**, solo lo detectan tardíamente.

---

## 6. Pruebas de Concurrencia Diseñadas / Ejecutadas

### 6.1 Pruebas Unitarias de Orden de Locks (Recomendadas)

```c
// Test: Verificar que dsm_sync_violations() == 0 tras secuencia típica
void test_lock_order_no_violations() {
    dsm_sync_set_check(1);
    dsm_sync_thread_reset();
    
    // Simular: fault → remote_fetch → local_install
    dsm_mutex_t global, page, frame;
    dsm_mutex_init(&global, DSM_LOCK_GLOBAL, "test");
    dsm_mutex_init(&page, DSM_LOCK_PAGE, "test");
    dsm_mutex_init(&frame, DSM_LOCK_FRAME, "test");
    
    dsm_mutex_lock(&global);
    dsm_mutex_lock(&page);   // OK: GLOBAL -> PAGE
    dsm_mutex_lock(&frame);  // OK: PAGE -> FRAME
    dsm_mutex_unlock(&frame);
    dsm_mutex_unlock(&page);
    dsm_mutex_unlock(&global);
    
    assert(dsm_sync_violations() == 0);
    
    // Inverso: debería detectar violación
    dsm_sync_thread_reset();
    dsm_mutex_lock(&page);
    dsm_mutex_lock(&global); // VIOLACIÓN: PAGE -> GLOBAL
    assert(dsm_sync_violations() == 1);
}
```

### 6.2 Pruebas de Estrés con Inyección de Fallos (Chaos Testing)

| Escenario | Inyección | Métrica de Éxito |
|-----------|-----------|------------------|
| **3 nodos, ping-pong propiedad** | Latencia de red variable (tc qdisc netem) | 0 deadlocks, 0 violaciones de orden, throughput > X ops/s |
| **Presión de memoria + evicción** | `frame_count` bajo, objetos grandes | `coherence_evict_one` nunca deadlockea, swap funciona |
| **Fallos concurrentes misma página** | Múltiples hilos fault en misma página | Solo un resolutor trae la página (`FETCHING` evita duplicados) |
| **Invalidación durante forward** | Owner recibe INVALIDATE mientras procesa PAGE_FORWARD | `handle_invalidate` espera cond, no deadlock |
| **Home cae durante RPC** | Kill home node mid-RPC | Cliente detecta `PEER_DOWN`, reintenta, no cuelga |
| **Barrier + fallos simultáneos** | Barrera global mientras hilos fault | Barrera termina, fallos se resuelven |

### 6.3 Pruebas de Regresión para Violaciones Conocidas

```c
// Test: dsm_coherence_snapshot no viola orden
void test_snapshot_lock_order() {
    dsm_sync_set_check(1);
    dsm_sync_thread_reset();
    
    dsm_page_snapshot_t snap;
    dsm_coherence_snapshot(co, page_id, &snap);
    
    // Si g_violations > 0, hay bug en orden de locks en snapshot
    assert(dsm_sync_violations() == 0);
}
```

### 6.4 Pruebas de Livelock en `coherence_make_room`

```c
// Verificar que make_room no entra en bucle infinito si todas las víctimas fallan por red
void test_make_room_network_failure() {
    // Configurar: todas las páginas sucias, peers caídos
    // Llamar coherence_make_room
    // Debe retornar DSM_ERR_NO_SPACE tras tries límite (64), no colgarse
}
```

### 6.5 Métricas de Observabilidad (Ya en Código)

- `dsm_stats_t` cuenta: `TIMEOUTS`, `RETRIES`, `ERRORS`, `EVICTIONS`, `EVICTIONS_DIRTY`, `FORWARDINGS`, `INVALIDATIONS_SENT/RECEIVED`, `REMOTE_READS/WRITES`, `LOCAL_HITS`, `PAGE_FAULTS`
- `dsm_sync_violations()` — contador atómico de violaciones de orden
- Logs `DSM_LOG_DEBUG/INFO/WARN/ERROR` con contexto de página, nodo, estado

---

## 7. Resumen de Hallazgos Críticos

| Severidad | Hallazgo | Archivo:Línea | Mitigación Actual | Acción Requerida |
|-----------|----------|---------------|-------------------|------------------|
| **ALTA** | `home_serve_write` mantiene `op_lock` (GLOBAL) durante múltiples RPCs salientes (`forward_from_owner` + bucle `invalidate_node`) | `coherence.c:1008-1091` | Fail-stop: si falla una invalidación, aborta todo y suelta lock | Rediseñar: soltar `op_lock` entre RPCs, usar versionado/optimista para detectar carreras |
| **MEDIA** | `dsm_coherence_snapshot` viola orden: toma `page->lock` (PAGE) luego `dir_lock`+`op_lock` (GLOBAL) | `coherence.c:2094-2104` | Solo usado por `dsmctl`/web (poco frecuente) | Reordenar: tomar `dir_lock`+`op_lock` ANTES que `page->lock`, o usar atomics para snapshot |
| **MEDIA** | `handle_invalidate` espera en `cond_timedwait` con `page->lock` (hasta timeout_ms) | `coherence.c:1549-1559` | Timeout configurable, `cond_timedwait` suelta lock | Considerar `cond_wait` sin límite + mecanismo de cancelación, o mover wait fuera del lock |
| **BAJA** | `local_drop` timeout duro 200ms puede abortar limpieza dejando página en estado intermedio | `coherence.c:489-499` | Log WARNING, retorna sin limpiar | Aumentar timeout o hacer wait indefinido con señal de cancelación |
| **BAJA** | `coherence_evict_one` hace `swap_out` (I/O disco) con `op_lock` **NO** tomado (OK), pero `local_install` en home toma `page->lock` → `frame_lock` | `coherence.c:721-735` | `swap_out` sin locks de página | Ninguna — correcto |

---

## 8. Conclusiones y Recomendaciones

### 8.1 Lo Que Está Bien
1. **Jerarquía de locks documentada y verificada en runtime** (`dsm_sync.h` + `sync.c`).
2. **Resolutor respeta regla de oro**: nunca espera red con page lock tomado (`coherence.c:1316`).
3. **Evicción suelta page lock antes de RPC** (`coherence.c:719` → `741`).
4. **`local_drop` y `handle_invalidate` usan `cond_timedwait`** que suelta lock durante espera.
5. **Timeouts en todas las operaciones de red** con backoff y presupuesto total.
6. **Cola de fallos lock-free** (`fault.c`) evita deadlock en signal handler.

### 8.2 Riesgos Residuales Aceptables para Prototipo
1. **`home_serve_write` con `op_lock` durante RPCs**: riesgo teórico de deadlock distribuido si home y owner se piden mutuamente con locks tomados. **Mitigación**: fail-stop + timeouts. En producción, separar fase de decisión (con lock) de fase de red (sin lock).
2. **`snapshot` viola orden**: solo afecta herramienta de diagnóstico, no camino crítico.

### 8.3 Próximos Pasos para Hardening
1. **Añadir test `test_lock_order_no_violations`** en CI.
2. **Ejecutar stress test 3-nodos** con `tc qdisc netem delay 10ms 5ms loss 1%` durante 30 min.
3. **Inyectar fallos de red** (`PEER_DOWN` simulado) en medio de `home_serve_write` y verificar recovery.
4. **Corregir `dsm_coherence_snapshot`** para respetar jerarquía.
5. **Considerar `pthread_mutex_trylock` + backoff** en `op_lock` para detectar contención extrema.

---

**Fin del Informe**  
*Generado por agente especial de análisis de deadlocks — solo auditoría, sin modificaciones de código.*