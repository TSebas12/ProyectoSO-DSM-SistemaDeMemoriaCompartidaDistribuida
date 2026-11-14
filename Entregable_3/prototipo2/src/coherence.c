/*
 * coherence.c — Protocolo MSI, directorio, objetos y barreras.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Reglas que ordenan todo el archivo:
 *
 *   1. Nunca se espera a la red con el lock de una página tomado. El hilo
 *      resolutor marca la página como TRAYENDO, suelta el lock, hace el RPC y
 *      vuelve a tomarlo para publicar el resultado.
 *   2. En el nodo home, las operaciones sobre una misma página se serializan
 *      con el op_lock de su entrada del directorio. Ese lock se toma antes de
 *      hablar con la red y se suelta al terminar; el lock de página solo se
 *      toma para leer o escribir metadatos, nunca durante un RPC.
 *   3. El home conserva siempre una copia (residente o en swap) de toda página
 *      de la que sea responsable. Por eso puede servir lecturas sin depender
 *      de que el dueño anterior siga vivo y por eso la expulsión local escribe
 *      al archivo de intercambio.
 *
 * Flujo de una lectura en un cliente:
 *
 *      fallo -> PAGE_REQUEST -> home -> (PAGE_FORWARD al dueño si hace falta)
 *            -> home copia los datos -> PAGE_DATA -> cliente en S
 *
 * Flujo de una escritura:
 *
 *      fallo -> OWNERSHIP_REQUEST -> home -> (PAGE_FORWARD al dueño)
 *            -> INVALIDATE a los terceros -> home suelta su copia
 *            -> OWNERSHIP_GRANT -> cliente en M
 */
#include "dsm_coherence.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_log.h"
#include "dsm_sync.h"

#define DSM_DIR_BUCKETS_DEFAULT 256u
#define DSM_TIMEOUT_MS_DEFAULT  3000u
#define DSM_BARRIER_POLL_MS     5u
#define DSM_EVICT_MIN_AGE_MS    50u

/* Secuencia de request_id del proceso. Cada nodo corre en su propio proceso. */
static _Atomic uint64_t g_request_seq = 1u;

static uint64_t next_request_id(void)
{
    return atomic_fetch_add_explicit(&g_request_seq, 1u, memory_order_relaxed);
}

static void stats_inc(dsm_stats_t *s, dsm_stats_field_t f)
{
    if (s != NULL) {
        dsm_stats_inc(s, f);
    }
}

static void stats_add(dsm_stats_t *s, dsm_stats_field_t f, uint64_t v)
{
    if (s != NULL) {
        dsm_stats_add(s, f, v);
    }
}

/* --- Directorio --------------------------------------------------------- */
static uint32_t dir_bucket_of(const dsm_coherence_t *co, uint64_t page_id)
{
    return (uint32_t)(page_id % (uint64_t)co->dir_buckets);
}

/* El llamador debe tener co->dir_lock tomado.                            */
static dsm_dir_entry_t *dir_find_locked(dsm_coherence_t *co, uint64_t page_id)
{
    dsm_dir_entry_t *e;

    for (e = co->dir[dir_bucket_of(co, page_id)]; e != NULL; e = e->next) {
        if (e->meta.page_id == page_id) {
            return e;
        }
    }
    return NULL;
}

/* El llamador debe tener co->dir_lock tomado.                            */
static dsm_dir_entry_t *dir_get_or_create_locked(dsm_coherence_t *co, uint64_t page_id)
{
    dsm_dir_entry_t *e;
    uint32_t         b;

    e = dir_find_locked(co, page_id);
    if (e != NULL) {
        return e;
    }
    e = calloc(1, sizeof(*e));
    if (e == NULL) {
        return NULL;
    }
    e->meta.page_id     = page_id;
    e->meta.home_node   = co->home_node;
    e->meta.owner_node  = co->home_node;
    e->meta.state       = (uint8_t)DSM_PAGE_I;
    e->meta.last_update_ms = dsm_now_ms();
    /*
     * op_lock es el lock más externo de la jerarquía: serializa las
     * operaciones del home sobre una página y se toma ANTES de consultar la
     * tabla de páginas o el objeto. Por eso se declara GLOBAL; el orden real
     * queda op_lock -> tabla/objetos -> página -> marco.
     */
    if (dsm_mutex_init(&e->op_lock, DSM_LOCK_GLOBAL, "pagina_home") != DSM_OK) {
        free(e);
        return NULL;
    }
    b = dir_bucket_of(co, page_id);
    e->next    = co->dir[b];
    co->dir[b] = e;
    return e;
}

/*
 * Devuelve la entrada del directorio con su op_lock TOMADO. El llamador debe
 * soltarlo con dsm_mutex_unlock(&e->op_lock). Devuelve NULL si no hay memoria.
 */
static dsm_dir_entry_t *dir_lock_entry(dsm_coherence_t *co, uint64_t page_id)
{
    dsm_dir_entry_t *e;

    if (dsm_mutex_lock(&co->dir_lock) != DSM_OK) {
        return NULL;
    }
    e = dir_get_or_create_locked(co, page_id);
    dsm_mutex_unlock(&co->dir_lock);
    if (e == NULL) {
        return NULL;
    }
    if (dsm_mutex_lock(&e->op_lock) != DSM_OK) {
        return NULL;
    }
    return e;
}

/* --- Objetos ------------------------------------------------------------ */
/* El llamador debe tener co->obj_lock tomado.                            */
static dsm_object_t *object_find_locked(dsm_coherence_t *co, uint64_t object_id,
                                        uint32_t tag)
{
    uint32_t i;

    for (i = 0; i < co->object_count; i++) {
        if (object_id != 0 && co->objects[i].object_id == object_id) {
            return &co->objects[i];
        }
        if (object_id == 0 && tag != 0 && co->objects[i].tag == tag) {
            return &co->objects[i];
        }
    }
    return NULL;
}

static uint64_t object_of_page(dsm_coherence_t *co, uint64_t page_id)
{
    uint64_t found = DSM_INVALID_OBJECT;
    uint32_t i;

    if (co == NULL || co->obj_lock.initialized == 0) {
        return DSM_INVALID_OBJECT;
    }
    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return DSM_INVALID_OBJECT;
    }
    for (i = 0; i < co->object_count; i++) {
        const dsm_object_t *o = &co->objects[i];

        if (page_id >= o->page_base && page_id < o->page_base + o->page_count) {
            found = o->object_id;
            break;
        }
    }
    dsm_mutex_unlock(&co->obj_lock);
    return found;
}

/* Registra un objeto localmente. Devuelve DSM_ERR_EXIST si ya estaba.     */
static dsm_status_t object_record(dsm_coherence_t *co, const dsm_alloc_reply_t *r,
                                  uint32_t tag, uint32_t owner)
{
    dsm_object_t *o;

    if (co == NULL || r == NULL) {
        return DSM_ERR_INVAL;
    }
    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    o = object_find_locked(co, r->object_id, 0);
    if (o != NULL) {
        dsm_mutex_unlock(&co->obj_lock);
        return DSM_ERR_EXIST;
    }
    if (co->object_count >= DSM_MAX_OBJECTS) {
        dsm_mutex_unlock(&co->obj_lock);
        return DSM_ERR_NO_SPACE;
    }
    o = &co->objects[co->object_count++];
    o->object_id  = r->object_id;
    o->page_base  = r->page_base;
    o->page_count = r->page_count;
    o->size       = (uint64_t)r->page_count * (uint64_t)dsm_memory_page_size(co->mem);
    o->owner_node = owner;
    o->tag        = tag;
    o->flags      = 0;
    dsm_mutex_unlock(&co->obj_lock);
    return DSM_OK;
}

/* --- Transiciones MSI --------------------------------------------------- */
dsm_msi_state_t dsm_msi_next_state(dsm_msi_state_t current, dsm_msi_event_t ev)
{
    switch (current) {
    case DSM_PAGE_I:
        if (ev == DSM_EV_LOCAL_READ) {
            return DSM_PAGE_S;
        }
        if (ev == DSM_EV_LOCAL_WRITE) {
            return DSM_PAGE_M;
        }
        return DSM_PAGE_I;

    case DSM_PAGE_S:
        if (ev == DSM_EV_LOCAL_WRITE) {
            return DSM_PAGE_M;
        }
        if (ev == DSM_EV_INVALIDATE) {
            return DSM_PAGE_I;
        }
        return DSM_PAGE_S;

    case DSM_PAGE_M:
        if (ev == DSM_EV_INVALIDATE || ev == DSM_EV_GRANT) {
            return DSM_PAGE_I;
        }
        if (ev == DSM_EV_REMOTE_READ || ev == DSM_EV_WRITEBACK) {
            return DSM_PAGE_S;
        }
        return DSM_PAGE_M;

    default:
        return DSM_PAGE_I;
    }
}

/* --- Ciclo de vida ------------------------------------------------------ */
dsm_status_t dsm_coherence_init(dsm_coherence_t *co, dsm_memory_t *mem,
                                dsm_stats_t *stats,
                                const dsm_coherence_params_t *p,
                                const dsm_coherence_ops_t *ops)
{
    if (co == NULL || mem == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(co, 0, sizeof(*co));

    co->mem       = mem;
    co->stats     = stats;
    co->node_id   = (p != NULL) ? p->node_id : 0u;
    co->home_node = (p != NULL) ? p->home_node : 0u;
    if (co->node_id >= DSM_MAX_NODES || co->home_node >= DSM_MAX_NODES) {
        return DSM_ERR_INVAL;
    }
    co->n_nodes = (p != NULL && p->n_nodes != 0) ? p->n_nodes : 1u;
    if (co->n_nodes > DSM_MAX_NODES) {
        co->n_nodes = DSM_MAX_NODES;
    }
    co->timeout_ms = (p != NULL && p->timeout_ms != 0) ? p->timeout_ms
                                                       : DSM_TIMEOUT_MS_DEFAULT;
    co->dir_buckets = (p != NULL && p->dir_buckets != 0) ? p->dir_buckets
                                                         : DSM_DIR_BUCKETS_DEFAULT;
    if (ops != NULL) {
        co->ops = *ops;
    }
    atomic_init(&co->next_request_id, 1u);

    co->dir = calloc(co->dir_buckets, sizeof(dsm_dir_entry_t *));
    if (co->dir == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_init(&co->dir_lock, DSM_LOCK_GLOBAL, "directorio") != DSM_OK) {
        free(co->dir);
        co->dir = NULL;
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_init(&co->obj_lock, DSM_LOCK_OBJECT, "objetos") != DSM_OK) {
        dsm_mutex_destroy(&co->dir_lock);
        free(co->dir);
        co->dir = NULL;
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_init(&co->barrier_lock, DSM_LOCK_OBJECT, "barrera") != DSM_OK) {
        dsm_mutex_destroy(&co->obj_lock);
        dsm_mutex_destroy(&co->dir_lock);
        free(co->dir);
        co->dir = NULL;
        return DSM_ERR_NOMEM;
    }
    co->initialized = 1;
    return DSM_OK;
}

void dsm_coherence_destroy(dsm_coherence_t *co)
{
    uint32_t i;

    if (co == NULL || !co->initialized) {
        return;
    }
    for (i = 0; i < co->dir_buckets; i++) {
        dsm_dir_entry_t *e = co->dir[i];

        while (e != NULL) {
            dsm_dir_entry_t *next = e->next;

            dsm_mutex_destroy(&e->op_lock);
            free(e);
            e = next;
        }
        co->dir[i] = NULL;
    }
    free(co->dir);
    co->dir = NULL;
    dsm_mutex_destroy(&co->barrier_lock);
    dsm_mutex_destroy(&co->obj_lock);
    dsm_mutex_destroy(&co->dir_lock);
    co->initialized = 0;
}

/* --- Envío de respuestas ------------------------------------------------ */
static dsm_status_t send_status(dsm_coherence_t *co, const dsm_msg_t *msg,
                                uint16_t type, dsm_status_t code)
{
    dsm_msg_t resp;

    if (co->ops.send == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(&resp, 0, sizeof(resp));
    resp.type       = type;
    resp.flags      = DSM_FLAG_RESPONSE;
    resp.src_node   = co->node_id;
    resp.dst_node   = msg->src_node;
    resp.request_id = msg->request_id;
    resp.page_id    = msg->page_id;
    resp.status     = (int32_t)code;
    return co->ops.send(co->ops.ctx, &resp);
}

static dsm_status_t send_error(dsm_coherence_t *co, const dsm_msg_t *msg,
                               dsm_status_t code)
{
    return send_status(co, msg, DSM_MSG_ERROR, code);
}

static dsm_status_t send_payload(dsm_coherence_t *co, const dsm_msg_t *msg,
                                 uint16_t type, const void *data, uint32_t len,
                                 dsm_status_t code)
{
    dsm_msg_t resp;

    if (co->ops.send == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(&resp, 0, sizeof(resp));
    resp.type       = type;
    resp.flags      = (uint16_t)(DSM_FLAG_RESPONSE |
                                 ((len > 0) ? DSM_FLAG_HAS_DATA : 0));
    resp.src_node   = co->node_id;
    resp.dst_node   = msg->src_node;
    resp.request_id = msg->request_id;
    resp.page_id    = msg->page_id;
    resp.status     = (int32_t)code;
    resp.data       = (const uint8_t *)data;
    resp.data_len   = len;
    return co->ops.send(co->ops.ctx, &resp);
}

/* RPC con verificación del estado de la respuesta.                       */
static dsm_status_t call_msg(dsm_coherence_t *co, const dsm_msg_t *req,
                             dsm_msg_t *resp, uint32_t timeout_ms)
{
    dsm_status_t st;

    if (co->ops.call == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(resp, 0, sizeof(*resp));
    st = co->ops.call(co->ops.ctx, req, resp, timeout_ms);
    if (st != DSM_OK) {
        return st;
    }
    if (resp->status < 0) {
        return (dsm_status_t)resp->status;
    }
    return DSM_OK;
}

/* --- Estado local de una página ----------------------------------------- */
/* Declaración adelantada: instalar una página puede requerir hacer sitio.  */
static void coherence_make_room(dsm_coherence_t *co, uint64_t avoid_page);

static dsm_page_entry_t *local_page(dsm_coherence_t *co, uint64_t page_id)
{
    dsm_page_entry_t *p = dsm_memory_page_lookup(co->mem, page_id);

    if (p != NULL) {
        return p;
    }
    return dsm_memory_page_get_or_create(co->mem, page_id,
                                         object_of_page(co, page_id));
}

/*
 * Reintenta la instalación: entre "hay marcos libres" y "toma uno" otro hilo
 * puede consumirlos, así que un solo intento devolvería NO_SPACE con el pool
 * lleno y vivo. Se suelta el lock de página antes de expulsar para no
 * interbloquear con el lock de la propia víctima.
 */
static dsm_status_t local_install(dsm_coherence_t *co, uint64_t page_id,
                                  const uint8_t *buf, dsm_msi_state_t state)
{
    dsm_page_entry_t *p;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    uint32_t          tries;

    p = local_page(co, page_id);
    if (p == NULL) {
        return DSM_ERR_NOMEM;
    }
    for (tries = 0; tries < 8u; tries++) {
        uint32_t slot;

        if (dsm_mutex_lock(&p->lock) != DSM_OK) {
            return DSM_ERR_STATE;
        }
        if (buf != NULL) {
            dsm_status_t st = dsm_memory_copy_in(co->mem, page_id, buf, page_size);

            if (st != DSM_OK) {
                dsm_mutex_unlock(&p->lock);
                return st;
            }
        }
        if (p->meta.frame_slot == DSM_INVALID_FRAME) {
            slot = dsm_memory_alloc_frame(co->mem, page_id);
            if (slot == DSM_INVALID_FRAME) {
                dsm_mutex_unlock(&p->lock);
                coherence_make_room(co, page_id);
                continue;
            }
            p->meta.frame_slot = slot;
        }
        p->meta.msi_state = (uint8_t)state;
        p->meta.residency = (uint8_t)DSM_RES_RESIDENT;
        p->meta.is_dirty  = (state == DSM_PAGE_M) ? 1u : 0u;
        p->meta.flags    &= (uint8_t)~DSM_PAGE_FLAG_SWAP;
        p->meta.version++;
        dsm_memory_protect(co->mem, page_id, state);
        dsm_memory_touch(co->mem, page_id);
        dsm_mutex_unlock(&p->lock);
        return DSM_OK;
    }
    return DSM_ERR_NO_SPACE;
}

/* Suelta la copia local (queda en I y sin marco).                        */
static void local_drop(dsm_coherence_t *co, uint64_t page_id)
{
    dsm_page_entry_t *p = dsm_memory_page_lookup(co->mem, page_id);

    if (p == NULL || dsm_mutex_lock(&p->lock) != DSM_OK) {
        return;
    }
    if (p->meta.residency == (uint8_t)DSM_RES_RESIDENT) {
        p->meta.msi_state = (uint8_t)DSM_PAGE_I;
        p->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
        p->meta.is_dirty  = 0;
        p->meta.version++;
        dsm_memory_protect(co->mem, page_id, DSM_PAGE_I);
        if (p->meta.frame_slot != DSM_INVALID_FRAME) {
            dsm_memory_release_frame(co->mem, p->meta.frame_slot);
            p->meta.frame_slot = DSM_INVALID_FRAME;
        }
    }
    dsm_mutex_unlock(&p->lock);
}

/*
 * Garantiza que el home tenga los datos de la página en su copia local y los
 * deja en buf. Si la página nunca se tocó pero pertenece a un objeto, la
 * materializa con ceros; si estaba en swap, la restaura.
 *
 * El llamador debe tener el op_lock del directorio (o ser el único hilo que
 * toca esa página).
 */
static dsm_status_t home_ensure_data(dsm_coherence_t *co, uint64_t page_id,
                                     uint8_t *buf)
{
    uint64_t obj;
    uint32_t page_size = dsm_memory_page_size(co->mem);
    uint32_t tries;

    obj = object_of_page(co, page_id);
    for (tries = 0; tries < 8u; tries++) {
        dsm_page_entry_t *p;
        dsm_status_t      st;
        int               from_swap = 0;

        p = dsm_memory_page_lookup(co->mem, page_id);
        if (p == NULL) {
            if (obj == DSM_INVALID_OBJECT) {
                return DSM_ERR_NOENT;
            }
            p = dsm_memory_page_get_or_create(co->mem, page_id, obj);
            if (p == NULL) {
                return DSM_ERR_NOMEM;
            }
        }

        if (dsm_mutex_lock(&p->lock) != DSM_OK) {
            return DSM_ERR_STATE;
        }

        if (p->meta.residency != (uint8_t)DSM_RES_RESIDENT) {
            if ((p->meta.flags & DSM_PAGE_FLAG_SWAP) != 0) {
                if (!dsm_memory_swap_enabled(co->mem)) {
                    dsm_mutex_unlock(&p->lock);
                    return DSM_ERR_NOENT;
                }
                if (dsm_memory_swap_in(co->mem, page_id) != DSM_OK) {
                    dsm_mutex_unlock(&p->lock);
                    return DSM_ERR_NO_SPACE;
                }
                from_swap = 1;
                if (p->meta.msi_state == (uint8_t)DSM_PAGE_I) {
                    p->meta.msi_state = (uint8_t)DSM_PAGE_M;
                }
            } else if (obj != DSM_INVALID_OBJECT) {
                /* Primera materialización de una página de objeto: ceros. */
                if (dsm_memory_fill(co->mem, page_id, 0) != DSM_OK) {
                    dsm_mutex_unlock(&p->lock);
                    return DSM_ERR_NO_SPACE;
                }
                p->meta.msi_state = (uint8_t)DSM_PAGE_M;
            } else {
                dsm_mutex_unlock(&p->lock);
                return DSM_ERR_NOENT;
            }
            if (p->meta.frame_slot == DSM_INVALID_FRAME) {
                uint32_t slot = dsm_memory_alloc_frame(co->mem, page_id);

                if (slot == DSM_INVALID_FRAME) {
                    /*
                     * Otro hilo pudo llevarse el marco libre entre la
                     * comprobación y la reserva. Se suelta el lock y se
                     * reintenta; el swap_in/fill ya hecho es idempotente y el
                     * flag SWAP no se limpia hasta tener marco.
                     */
                    dsm_mutex_unlock(&p->lock);
                    coherence_make_room(co, page_id);
                    continue;
                }
                p->meta.frame_slot = slot;
            }
            if (from_swap) {
                p->meta.flags &= (uint8_t)~DSM_PAGE_FLAG_SWAP;
            }
            p->meta.residency = (uint8_t)DSM_RES_RESIDENT;
            p->meta.is_dirty  = (p->meta.msi_state == (uint8_t)DSM_PAGE_M) ? 1u : 0u;
            dsm_memory_protect(co->mem, page_id, (dsm_msi_state_t)p->meta.msi_state);
        }

        st = dsm_memory_copy_out(co->mem, page_id, buf, page_size);
        dsm_memory_touch(co->mem, page_id);
        dsm_mutex_unlock(&p->lock);
        return st;
    }
    return DSM_ERR_NO_SPACE;
}

/* --- Expulsión ---------------------------------------------------------- */
static dsm_status_t release_to_home(dsm_coherence_t *co, uint64_t page_id,
                                    const uint8_t *data, uint32_t len)
{
    dsm_msg_t    req;
    dsm_msg_t    resp;
    dsm_status_t st;

    memset(&req, 0, sizeof(req));
    req.type       = DSM_MSG_PAGE_RELEASE;
    req.src_node   = co->node_id;
    req.dst_node   = co->home_node;
    req.page_id    = page_id;
    req.request_id = next_request_id();
    req.data       = data;
    req.data_len   = len;

    DSM_LOG(DSM_LOG_DEBUG, "release_to_home: sending PAGE_RELEASE for page %llu to home %u",
            (unsigned long long)page_id, co->home_node);
    st = call_msg(co, &req, &resp, co->timeout_ms);
    if (resp.data != NULL) {
        free((void *)resp.data);
    }
    DSM_LOG(DSM_LOG_DEBUG, "release_to_home: call_msg returned %d for page %llu",
            (int)st, (unsigned long long)page_id);
    if (st == DSM_OK && resp.type != DSM_MSG_ACK) {
        return DSM_ERR_PROTOCOL;
    }
    if (st == DSM_OK) {
        stats_inc(co->stats, DSM_STAT_RELEASES);
    }
    return st;
}

/*
 * Elige una víctima y la expulsa. En el home la escribe al swap (puede ser la
 * única copia); en un cliente sucio la devuelve al home antes de soltarla.
 * Devuelve DSM_OK si liberó un marco.
 */
static dsm_status_t coherence_evict_one(dsm_coherence_t *co, uint64_t avoid_page)
{
    uint64_t          victim;
    dsm_page_entry_t *p;
    uint8_t          *buf = NULL;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    uint8_t           state;
    int               dirty;
    dsm_status_t      st = DSM_OK;

    victim = dsm_memory_pick_victim(co->mem, DSM_EVICT_MIN_AGE_MS, avoid_page);
    if (victim == DSM_INVALID_PAGE_ID) {
        victim = dsm_memory_pick_victim(co->mem, 0u, avoid_page);
    }
    if (victim == DSM_INVALID_PAGE_ID) {
        DSM_LOG(DSM_LOG_DEBUG, "evict: no victim found (avoid=%llu)", (unsigned long long)avoid_page);
        return DSM_ERR_NO_SPACE;
    }
    p = dsm_memory_page_lookup(co->mem, victim);
    if (p == NULL || dsm_mutex_lock(&p->lock) != DSM_OK) {
        DSM_LOG(DSM_LOG_DEBUG, "evict: no page entry or lock fail for %llu", (unsigned long long)victim);
        return DSM_ERR_NOENT;
    }
    DSM_LOG(DSM_LOG_DEBUG, "evict: trying victim %llu residency=%d msi=%d dirty=%d",
            (unsigned long long)victim, (int)p->meta.residency, (int)p->meta.msi_state, (int)p->meta.is_dirty);
    if (p->meta.residency != (uint8_t)DSM_RES_RESIDENT) {
        dsm_mutex_unlock(&p->lock);
        DSM_LOG(DSM_LOG_DEBUG, "evict: victim %llu not resident (state=%d)", (unsigned long long)victim, (int)p->meta.residency);
        return DSM_ERR_AGAIN;
    }
    state = p->meta.msi_state;
    dirty = (p->meta.is_dirty != 0);
    if (dirty) {
        buf = malloc(page_size);
        if (buf != NULL &&
            dsm_memory_copy_out(co->mem, victim, buf, page_size) != DSM_OK) {
            free(buf);
            dsm_mutex_unlock(&p->lock);
            return DSM_ERR_STATE;
        }
    }
    dsm_mutex_unlock(&p->lock);

    if (co->node_id == co->home_node) {
        /* El home nunca pierde datos: los conserva en el archivo de swap. */
        if (!dsm_memory_swap_enabled(co->mem) ||
            dsm_memory_swap_out(co->mem, victim) != DSM_OK) {
            DSM_LOG(DSM_LOG_ERROR, "no se pudo escribir al swap la pagina %llu",
                    (unsigned long long)victim);
            free(buf);
            return DSM_ERR_NO_SPACE;
        }
        stats_add(co->stats, DSM_STAT_SWAP_BYTES, (uint64_t)page_size);
    } else {
        /* Un cliente sucio devuelve la página; uno limpio solo se desregistra. */
        DSM_LOG(DSM_LOG_DEBUG, "evict: releasing page %llu to home (dirty=%d)",
                (unsigned long long)victim, dirty);
        st = release_to_home(co, victim, (dirty ? buf : NULL),
                             (dirty ? page_size : 0u));
        if (st != DSM_OK) {
            DSM_LOG(DSM_LOG_WARN, "no se pudo liberar la pagina %llu al home: %d",
                    (unsigned long long)victim, (int)st);
            free(buf);
            return st;
        }
        DSM_LOG(DSM_LOG_DEBUG, "evict: page %llu released to home OK",
                (unsigned long long)victim);
    }
    free(buf);

    if (dsm_mutex_lock(&p->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    if (p->meta.residency == (uint8_t)DSM_RES_RESIDENT &&
        p->meta.msi_state == state) {
        p->meta.msi_state = (uint8_t)DSM_PAGE_I;
        p->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
        p->meta.is_dirty  = 0;
        p->meta.version++;
        if (co->node_id == co->home_node) {
            p->meta.flags |= (uint8_t)DSM_PAGE_FLAG_SWAP;
        } else {
            p->meta.flags &= (uint8_t)~DSM_PAGE_FLAG_SWAP;
        }
        dsm_memory_protect(co->mem, victim, DSM_PAGE_I);
        if (p->meta.frame_slot != DSM_INVALID_FRAME) {
            dsm_memory_release_frame(co->mem, p->meta.frame_slot);
            p->meta.frame_slot = DSM_INVALID_FRAME;
        }
    }
    dsm_mutex_unlock(&p->lock);

    stats_inc(co->stats, DSM_STAT_EVICTIONS);
    if (dirty) {
        stats_inc(co->stats, DSM_STAT_EVICTIONS_DIRTY);
    }
    return DSM_OK;
}

static void coherence_make_room(dsm_coherence_t *co, uint64_t avoid_page)
{
    uint64_t resident = 0;
    uint64_t free_frames = 0;
    uint32_t tries = 0;
    dsm_status_t last = DSM_OK;

    dsm_memory_usage(co->mem, &resident, &free_frames);
    DSM_LOG(DSM_LOG_DEBUG, "make_room: start resident=%llu free=%llu avoid=%llu",
            (unsigned long long)resident, (unsigned long long)free_frames, (unsigned long long)avoid_page);
    /*
     * Se insiste unas cuantas veces: la primera víctima puede estar en una
     * transición (no residente) y no liberar marco. El tope evita un bucle
     * infinito si de verdad no hay nada expulsable.
     *
     * Si una víctima falla por error de red (peer down), se salta y se prueba
     * con otra; solo se aborta si se agotan los intentos o el error es interno.
     */
    while (free_frames == 0 && tries < 64u) {
        dsm_status_t st = coherence_evict_one(co, avoid_page);

        last = st;
        tries++;
        /*
         * AGAIN: la víctima cambió de estado, probar otra.
         * PEER_DOWN/TIMEOUT/NET: la víctima es sucia y no se pudo enviar al home;
         *   saltarla y probar otra víctima (quizás limpia).
         * Otros errores: fallo interno, abortar.
         */
        if (st == DSM_ERR_AGAIN) {
            /* seguir */
        } else if (st == DSM_ERR_PEER_DOWN || st == DSM_ERR_TIMEOUT || st == DSM_ERR_NET) {
            DSM_LOG(DSM_LOG_WARN, "make_room: víctima %u fallo de red (%d), probando otra",
                    (unsigned)tries, (int)st);
            /* seguir probando otras víctimas */
        } else if (st != DSM_OK) {
            break;
        }
        dsm_memory_usage(co->mem, &resident, &free_frames);
    }
    if (free_frames == 0) {
        DSM_LOG(DSM_LOG_ERROR,
                "sin marcos: residentes=%llu intentos=%u ultimo_estado=%d",
                (unsigned long long)resident, (unsigned)tries, (int)last);
    } else {
        DSM_LOG(DSM_LOG_DEBUG, "make_room: success after %u tries, free=%llu",
                (unsigned)tries, (unsigned long long)free_frames);
    }
}

uint32_t dsm_coherence_force_eviction(dsm_coherence_t *co, uint32_t count)
{
    uint32_t done = 0;
    uint32_t i;

    if (co == NULL || !co->initialized) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (coherence_evict_one(co, DSM_INVALID_PAGE_ID) != DSM_OK) {
            break;
        }
        done++;
    }
    return done;
}

/* --- Reenvío desde el dueño actual -------------------------------------- */
/*
 * Pide la página al nodo que la tiene en M. Si exclusive, el dueño la suelta
 * (pasa a I); si no, se queda con una copia S. El home instala la copia.
 */
static dsm_status_t forward_from_owner(dsm_coherence_t *co, dsm_dir_entry_t *d,
                                       uint32_t owner, int exclusive, uint8_t *buf)
{
    dsm_msg_t    req;
    dsm_msg_t    resp;
    uint32_t     page_size = dsm_memory_page_size(co->mem);
    dsm_status_t st;

    memset(&req, 0, sizeof(req));
    req.type       = DSM_MSG_PAGE_FORWARD;    req.flags      = exclusive ? DSM_FLAG_LAST : 0u;
    req.src_node   = co->node_id;
    req.dst_node   = owner;
    req.page_id    = d->meta.page_id;
    req.request_id = next_request_id();

    st = call_msg(co, &req, &resp, co->timeout_ms);
    if (st != DSM_OK) {
        if (st == DSM_ERR_TIMEOUT) {
            stats_inc(co->stats, DSM_STAT_TIMEOUTS);
        } else {
            stats_inc(co->stats, DSM_STAT_ERRORS);
        }
        return st;
    }
    if (resp.type != DSM_MSG_PAGE_DATA || resp.data_len != page_size) {
        if (resp.data != NULL) {
            free((void *)resp.data);
        }
        return DSM_ERR_PROTOCOL;
    }
    memcpy(buf, resp.data, page_size);
    free((void *)resp.data);

    /* El home se queda con una copia limpia para poder servir el futuro.
     * local_install hace sitio por su cuenta si el pool está lleno.        */
    st = local_install(co, d->meta.page_id, buf, DSM_PAGE_S);
    if (st != DSM_OK) {
        return st;
    }
    if (exclusive) {
        if (owner < 64u) {
            d->meta.sharers &= ~(1ull << owner);
        }
    } else if (owner < 64u) {
        d->meta.sharers |= (1ull << owner);
    }
    d->meta.owner_node = co->home_node;
    d->meta.state      = (uint8_t)DSM_PAGE_S;
    d->meta.version++;
    d->meta.last_update_ms = dsm_now_ms();
    stats_inc(co->stats, DSM_STAT_FORWARDINGS);
    return DSM_OK;
}

static dsm_status_t invalidate_node(dsm_coherence_t *co, uint64_t page_id,
                                    uint32_t node, int need_data,
                                    uint8_t *out, uint32_t *out_len)
{
    dsm_msg_t    req;
    dsm_msg_t    resp;
    dsm_status_t st;

    memset(&req, 0, sizeof(req));
    req.type       = DSM_MSG_INVALIDATE;
    req.flags      = need_data ? DSM_FLAG_NEED_DATA : 0u;
    req.src_node   = co->node_id;
    req.dst_node   = node;
    req.page_id    = page_id;
    req.request_id = next_request_id();

    st = call_msg(co, &req, &resp, co->timeout_ms);
    if (st != DSM_OK) {
        if (st == DSM_ERR_TIMEOUT) {
            stats_inc(co->stats, DSM_STAT_TIMEOUTS);
        } else {
            stats_inc(co->stats, DSM_STAT_ERRORS);
        }
        return st;
    }
    if (resp.type != DSM_MSG_INVALIDATE_ACK) {
        if (resp.data != NULL) {
            free((void *)resp.data);
        }
        return DSM_ERR_PROTOCOL;
    }
    stats_inc(co->stats, DSM_STAT_INVALIDATIONS_SENT);
    if (need_data && out != NULL && out_len != NULL &&
        resp.data_len == dsm_memory_page_size(co->mem)) {
        memcpy(out, resp.data, resp.data_len);
        *out_len = resp.data_len;
    }
    if (resp.data != NULL) {
        free((void *)resp.data);
    }
    return DSM_OK;
}

/* --- Servicio del home -------------------------------------------------- */
static dsm_status_t home_serve_read(dsm_coherence_t *co, dsm_dir_entry_t *d,
                                    uint32_t requester, uint8_t *buf)
{
    uint64_t     page_id = d->meta.page_id;
    uint32_t     owner   = d->meta.owner_node;
    dsm_status_t st;

    if (owner != co->home_node) {
        st = forward_from_owner(co, d, owner, 0, buf);
        if (st != DSM_OK) {
            return st;
        }
    } else {
        st = home_ensure_data(co, page_id, buf);
        if (st != DSM_OK) {
            return st;
        }
        d->meta.owner_node = co->home_node;
        d->meta.state      = (uint8_t)DSM_PAGE_S;
    }

    if (requester < 64u) {
        d->meta.sharers |= (1ull << requester);
    }
    d->meta.version++;
    d->meta.last_update_ms = dsm_now_ms();
    return DSM_OK;
}

static dsm_status_t home_serve_write(dsm_coherence_t *co, dsm_dir_entry_t *d,
                                     uint32_t requester, uint8_t *buf)
{
    uint64_t     page_id = d->meta.page_id;
    uint32_t     owner   = d->meta.owner_node;
    uint64_t     others;
    uint32_t     node;
    dsm_status_t st;

    if (owner != co->home_node) {
        st = forward_from_owner(co, d, owner, 1, buf);
        if (st != DSM_OK) {
            return st;
        }
    } else {
        st = home_ensure_data(co, page_id, buf);
        if (st != DSM_OK) {
            return st;
        }
        d->meta.owner_node = co->home_node;
    }

    /*
     * Invalidación de terceros: sin esto dos nodos podrían escribir la misma
     * página y la última escritura en llegar borraría a la otra.
     */
    others = d->meta.sharers & ~(1ull << requester) & ~(1ull << co->home_node);
    for (node = 0; node < 64u; node++) {
        if ((others & (1ull << node)) == 0ull) {
            continue;
        }
        st = invalidate_node(co, page_id, node, 0, NULL, NULL);
        if (st != DSM_OK) {
            /* Política fail-stop: no se concede la propiedad a medias.    */
            return st;
        }
        d->meta.sharers &= ~(1ull << node);
    }

    if (requester < 64u) {
        d->meta.sharers |= (1ull << requester);
    }
    d->meta.owner_node = requester;
    d->meta.state      = (uint8_t)DSM_PAGE_M;
    d->meta.version++;
    d->meta.last_update_ms = dsm_now_ms();

    if (requester != co->home_node) {
        /* El home entrega la única copia modificable y suelta la suya.   */
        local_drop(co, page_id);
    }
    return DSM_OK;
}

/* --- Resolución de fallos ----------------------------------------------- */
static dsm_status_t local_fetch(dsm_coherence_t *co, uint64_t page_id,
                                int is_read, uint8_t *buf)
{
    dsm_dir_entry_t *d;
    dsm_status_t     st;

    d = dir_lock_entry(co, page_id);
    if (d == NULL) {
        return DSM_ERR_NOMEM;
    }
    st = is_read ? home_serve_read(co, d, co->node_id, buf)
                 : home_serve_write(co, d, co->node_id, buf);
    dsm_mutex_unlock(&d->op_lock);
    return st;
}

static dsm_status_t remote_fetch(dsm_coherence_t *co, uint64_t page_id,
                                 int is_read, uint8_t *buf)
{
    dsm_msg_t    req;
    dsm_msg_t    resp;
    uint16_t     want;
    uint32_t     page_size = dsm_memory_page_size(co->mem);
    dsm_status_t st;

    memset(&req, 0, sizeof(req));
    req.type       = is_read ? DSM_MSG_PAGE_REQUEST : DSM_MSG_OWNERSHIP_REQUEST;
    req.src_node   = co->node_id;
    req.dst_node   = co->home_node;
    req.page_id    = page_id;
    req.request_id = next_request_id();

    st = call_msg(co, &req, &resp, co->timeout_ms);
    if (st != DSM_OK) {
        if (st == DSM_ERR_TIMEOUT) {
            stats_inc(co->stats, DSM_STAT_TIMEOUTS);
        } else {
            stats_inc(co->stats, DSM_STAT_ERRORS);
        }
        return st;
    }
    want = is_read ? DSM_MSG_PAGE_DATA : DSM_MSG_OWNERSHIP_GRANT;
    if (resp.type != want || resp.data_len != page_size) {
        if (resp.data != NULL) {
            free((void *)resp.data);
        }
        return DSM_ERR_PROTOCOL;
    }
    memcpy(buf, resp.data, page_size);
    free((void *)resp.data);

    stats_inc(co->stats, is_read ? DSM_STAT_REMOTE_READS : DSM_STAT_REMOTE_WRITES);
    return DSM_OK;
}

dsm_status_t dsm_coherence_handle_fault(dsm_coherence_t *co, const dsm_fault_req_t *req)
{
    dsm_page_entry_t *page;
    uint64_t          page_id;
    uint8_t          *buf;
    uint32_t          page_size;
    int               is_read;
    dsm_status_t      st;

    if (co == NULL || req == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }
    page_id   = req->page_id;
    page_size = dsm_memory_page_size(co->mem);
    /* Un fallo desconocido se trata como escritura: pedir propiedad siempre
     * es correcto, aunque cueste una invalidación de más. */
    is_read = (req->kind == DSM_FAULT_READ);

    page = dsm_memory_page_get_or_create(co->mem, page_id,
                                         object_of_page(co, page_id));
    if (page == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }

    for (;;) {
        dsm_residency_t res = (dsm_residency_t)page->meta.residency;

        if (res == DSM_RES_FETCHING) {
            if (dsm_cond_wait(&page->cond, &page->lock) != DSM_OK) {
                dsm_mutex_unlock(&page->lock);
                return DSM_ERR_STATE;
            }
            continue;
        }
        if (res == DSM_RES_ERROR) {
            dsm_mutex_unlock(&page->lock);
            return DSM_ERR_PAGE_FAULT;
        }
        if (res == DSM_RES_RESIDENT) {
            dsm_msi_state_t state = (dsm_msi_state_t)page->meta.msi_state;

            if ((is_read && state != DSM_PAGE_I) ||
                (!is_read && state == DSM_PAGE_M)) {
                dsm_stats_inc(co->stats, DSM_STAT_LOCAL_HITS);
                dsm_memory_touch(co->mem, page_id);
                dsm_mutex_unlock(&page->lock);
                return DSM_OK;
            }
        }
        /* Residente en S y se pide escritura: hay que subir a M.         */
        break;
    }

    page->meta.residency = (uint8_t)DSM_RES_FETCHING;
    dsm_mutex_unlock(&page->lock);

    buf = malloc(page_size);
    if (buf == NULL) {
        if (dsm_mutex_lock(&page->lock) == DSM_OK) {
            page->meta.residency = (uint8_t)DSM_RES_ERROR;
            dsm_cond_broadcast(&page->cond);
            dsm_mutex_unlock(&page->lock);
        }
        return DSM_ERR_NOMEM;
    }

    st = (co->node_id == co->home_node)
         ? local_fetch(co, page_id, is_read, buf)
         : remote_fetch(co, page_id, is_read, buf);

    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        free(buf);
        return DSM_ERR_STATE;
    }
    if (st == DSM_OK) {
        uint32_t tries = 0;

        /*
         * El marco se reserva aquí, con el lock de página tomado. Si el pool
         * está lleno no se puede expulsar sin soltar el lock (la víctima
         * podría ser esta misma página), así que se suelta, se hace sitio y se
         * vuelve a tomar. La página sigue en FETCHING: ningún otro hilo la
         * toca mientras tanto.
         */
        while (page->meta.frame_slot == DSM_INVALID_FRAME) {
            uint32_t slot = dsm_memory_alloc_frame(co->mem, page_id);

            if (slot != DSM_INVALID_FRAME) {
                page->meta.frame_slot = slot;
                break;
            }
            if (tries >= 8u) {
                st = DSM_ERR_NO_SPACE;
                break;
            }
            tries++;
            dsm_mutex_unlock(&page->lock);
            coherence_make_room(co, page_id);
            if (dsm_mutex_lock(&page->lock) != DSM_OK) {
                free(buf);
                return DSM_ERR_STATE;
            }
        }
        if (st == DSM_OK) {
            st = dsm_memory_copy_in(co->mem, page_id, buf, page_size);
        }
    }
    if (st == DSM_OK) {
        dsm_msi_state_t state = is_read ? DSM_PAGE_S : DSM_PAGE_M;

        page->meta.msi_state = (uint8_t)state;
        page->meta.residency = (uint8_t)DSM_RES_RESIDENT;
        page->meta.is_dirty  = (state == DSM_PAGE_M) ? 1u : 0u;
        page->meta.flags    &= (uint8_t)~DSM_PAGE_FLAG_SWAP;
        page->meta.version++;
        dsm_memory_protect(co->mem, page_id, state);
        dsm_memory_touch(co->mem, page_id);
        dsm_stats_inc(co->stats, DSM_STAT_PAGE_FAULTS);
    } else {
        page->meta.residency = (uint8_t)DSM_RES_ERROR;
        dsm_stats_inc(co->stats, DSM_STAT_ERRORS);
    }
    dsm_cond_broadcast(&page->cond);
    dsm_mutex_unlock(&page->lock);
    free(buf);
    return st;
}

/* --- Lado servidor: mensajes entrantes ---------------------------------- */
static dsm_status_t handle_page_request(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_dir_entry_t *d;
    uint8_t         *buf;
    uint32_t         page_size = dsm_memory_page_size(co->mem);
    dsm_status_t     st;

    if (co->node_id != co->home_node) {
        /* Solo el home tiene directorio; un cliente no puede servir esto. */
        return send_error(co, msg, DSM_ERR_NOTSUP);
    }
    buf = malloc(page_size);
    if (buf == NULL) {
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    d = dir_lock_entry(co, msg->page_id);
    if (d == NULL) {
        free(buf);
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    st = home_serve_read(co, d, msg->src_node, buf);
    dsm_mutex_unlock(&d->op_lock);

    if (st != DSM_OK) {
        free(buf);
        return send_error(co, msg, st);
    }
    st = send_payload(co, msg, DSM_MSG_PAGE_DATA, buf, page_size, DSM_OK);
    free(buf);
    return st;
}

static dsm_status_t handle_ownership_request(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_dir_entry_t *d;
    uint8_t         *buf;
    uint32_t         page_size = dsm_memory_page_size(co->mem);
    dsm_status_t     st;

    if (co->node_id != co->home_node) {
        return send_error(co, msg, DSM_ERR_NOTSUP);
    }
    buf = malloc(page_size);
    if (buf == NULL) {
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    d = dir_lock_entry(co, msg->page_id);
    if (d == NULL) {
        free(buf);
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    st = home_serve_write(co, d, msg->src_node, buf);
    dsm_mutex_unlock(&d->op_lock);

    if (st != DSM_OK) {
        free(buf);
        return send_error(co, msg, st);
    }
    st = send_payload(co, msg, DSM_MSG_OWNERSHIP_GRANT, buf, page_size, DSM_OK);
    free(buf);
    return st;
}

/* PAGE_FORWARD: el home pide la página al dueño actual.                  */
static dsm_status_t handle_page_forward(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    uint8_t          *buf;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    dsm_msi_state_t   nuevo;
    int               exclusive;

    page = dsm_memory_page_lookup(co->mem, msg->page_id);
    if (page == NULL) {
        return send_error(co, msg, DSM_ERR_NOENT);
    }
    buf = malloc(page_size);
    if (buf == NULL) {
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        free(buf);
        return send_error(co, msg, DSM_ERR_STATE);
    }
    if (page->meta.residency != (uint8_t)DSM_RES_RESIDENT ||
        page->meta.msi_state == (uint8_t)DSM_PAGE_I) {
        dsm_mutex_unlock(&page->lock);
        free(buf);
        return send_error(co, msg, DSM_ERR_NOENT);
    }

    if (dsm_memory_copy_out(co->mem, msg->page_id, buf, page_size) != DSM_OK) {
        dsm_mutex_unlock(&page->lock);
        free(buf);
        return send_error(co, msg, DSM_ERR_STATE);
    }
    exclusive = (msg->flags & DSM_FLAG_LAST) != 0u;
    nuevo = exclusive ? DSM_PAGE_I : DSM_PAGE_S;
    page->meta.msi_state = (uint8_t)nuevo;
    page->meta.is_dirty  = 0;
    page->meta.version++;
    dsm_memory_protect(co->mem, msg->page_id, nuevo);
    if (exclusive) {
        page->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
        if (page->meta.frame_slot != DSM_INVALID_FRAME) {
            dsm_memory_release_frame(co->mem, page->meta.frame_slot);
            page->meta.frame_slot = DSM_INVALID_FRAME;
        }
    }
    dsm_mutex_unlock(&page->lock);

    {
        dsm_status_t st = send_payload(co, msg, DSM_MSG_PAGE_DATA, buf,
                                       page_size, DSM_OK);

        free(buf);
        return st;
    }
}

/* INVALIDATE: el nodo descarta su copia y confirma.                      */
static dsm_status_t handle_invalidate(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    uint8_t          *data = NULL;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    int               had_copy = 0;

    page = dsm_memory_page_lookup(co->mem, msg->page_id);
    if (page != NULL && dsm_mutex_lock(&page->lock) == DSM_OK) {
        if (page->meta.residency == (uint8_t)DSM_RES_RESIDENT &&
            page->meta.msi_state != (uint8_t)DSM_PAGE_I) {
            had_copy = 1;
            if ((msg->flags & DSM_FLAG_NEED_DATA) != 0u &&
                page->meta.is_dirty != 0) {
                data = malloc(page_size);
                if (data != NULL &&
                    dsm_memory_copy_out(co->mem, msg->page_id, data,
                                        page_size) != DSM_OK) {
                    free(data);
                    data = NULL;
                }
            }
            page->meta.msi_state = (uint8_t)DSM_PAGE_I;
            page->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
            page->meta.is_dirty  = 0;
            page->meta.version++;
            dsm_memory_protect(co->mem, msg->page_id, DSM_PAGE_I);
            if (page->meta.frame_slot != DSM_INVALID_FRAME) {
                dsm_memory_release_frame(co->mem, page->meta.frame_slot);
                page->meta.frame_slot = DSM_INVALID_FRAME;
            }
        }
        dsm_mutex_unlock(&page->lock);
    }
    if (had_copy) {
        stats_inc(co->stats, DSM_STAT_INVALIDATIONS_RECEIVED);
    }

    {
        dsm_status_t st = send_payload(co, msg, DSM_MSG_INVALIDATE_ACK, data,
                                       (data != NULL) ? page_size : 0u, DSM_OK);

        free(data);
        return st;
    }
}

/* PAGE_WRITEBACK: el home recibe una página sucia (compatibilidad E2).   */
static dsm_status_t handle_writeback(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    uint32_t     page_size = dsm_memory_page_size(co->mem);
    dsm_status_t st;

    if (msg->data == NULL || msg->data_len != page_size) {
        return send_error(co, msg, DSM_ERR_INVAL);
    }
    st = local_install(co, msg->page_id, msg->data, DSM_PAGE_S);
    if (st != DSM_OK) {
        return send_error(co, msg, st);
    }
    if (co->node_id == co->home_node) {
        dsm_dir_entry_t *d = dir_lock_entry(co, msg->page_id);

        if (d != NULL) {
            if (msg->src_node < 64u) {
                d->meta.sharers |= (1ull << msg->src_node);
            }
            d->meta.owner_node = co->home_node;
            d->meta.state      = (uint8_t)DSM_PAGE_S;
            d->meta.version++;
            dsm_mutex_unlock(&d->op_lock);
        }
    }
    return send_status(co, msg, DSM_MSG_PAGE_WRITEBACK_ACK, DSM_OK);
}

/* PAGE_RELEASE: un nodo suelta su copia (expulsión o fin de uso).        */
static dsm_status_t handle_page_release(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_dir_entry_t *d;
    uint32_t         page_size = dsm_memory_page_size(co->mem);
    dsm_status_t     st = DSM_OK;

    if (co->node_id != co->home_node) {
        return send_error(co, msg, DSM_ERR_NOTSUP);
    }
    d = dir_lock_entry(co, msg->page_id);
    if (d == NULL) {
        return send_error(co, msg, DSM_ERR_NOMEM);
    }

    if (msg->src_node < 64u) {
        d->meta.sharers &= ~(1ull << msg->src_node);
    }
    if (d->meta.owner_node == msg->src_node) {
        if (msg->data != NULL && msg->data_len == page_size) {
            st = local_install(co, msg->page_id, msg->data, DSM_PAGE_S);
        }
        d->meta.owner_node = co->home_node;
        d->meta.state = (d->meta.sharers != 0ull) ? (uint8_t)DSM_PAGE_S
                                                  : (uint8_t)DSM_PAGE_I;
    } else if (d->meta.sharers == 0ull && d->meta.owner_node == co->home_node) {
        d->meta.state = (uint8_t)DSM_PAGE_I;
    }
    d->meta.version++;
    d->meta.last_update_ms = dsm_now_ms();
    dsm_mutex_unlock(&d->op_lock);

    if (st != DSM_OK) {
        return send_error(co, msg, st);
    }
    return send_status(co, msg, DSM_MSG_ACK, DSM_OK);
}

/* OBJECT_ALLOC: asignador monótono del home.                             */
/*
 * Asignación determinista: la secuencia `seq` identifica la llamada lógica a
 * dsm_malloc. Si ya se atendió esa secuencia se devuelve el mismo rango; si no,
 * se reserva un tramo nuevo del espacio global. Todos los nodos que pidan la
 * misma secuencia reciben el mismo page_base y el mismo object_id.
 */
static dsm_status_t home_alloc_object(dsm_coherence_t *co, uint32_t src,
                                      uint32_t seq, uint64_t size,
                                      uint32_t tag, dsm_alloc_reply_t *out)
{
    uint32_t page_size = dsm_memory_page_size(co->mem);
    uint64_t pages;

    if (out == NULL || size == 0) {
        return DSM_ERR_INVAL;
    }
    if (seq < DSM_MAX_OBJECTS && co->alloc_cache[seq].valid != 0) {
        *out = co->alloc_cache[seq].reply;
        return DSM_OK;
    }

    pages = (size + (uint64_t)page_size - 1u) / (uint64_t)page_size;
    if (pages == 0) {
        pages = 1;
    }

    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    if (co->object_count >= DSM_MAX_OBJECTS ||
        co->next_page + pages > (uint64_t)dsm_memory_page_count(co->mem)) {
        dsm_mutex_unlock(&co->obj_lock);
        return DSM_ERR_NO_SPACE;
    }
    memset(out, 0, sizeof(*out));
    out->object_id  = ++co->object_seq;
    out->page_base  = co->next_page;
    out->page_count = (uint32_t)pages;
    out->status     = (uint32_t)DSM_OK;
    co->next_page  += pages;
    dsm_mutex_unlock(&co->obj_lock);

    (void)object_record(co, out, tag, src);

    if (seq < DSM_MAX_OBJECTS) {
        co->alloc_cache[seq].reply = *out;
        co->alloc_cache[seq].valid = 1;
    }
    return DSM_OK;
}

static dsm_status_t handle_object_alloc(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    const dsm_alloc_req_t *req;
    dsm_alloc_reply_t      reply;
    dsm_status_t           st;

    if (co->node_id != co->home_node) {
        return send_error(co, msg, DSM_ERR_NOTSUP);
    }
    if (msg->data == NULL || msg->data_len < sizeof(dsm_alloc_req_t)) {
        return send_error(co, msg, DSM_ERR_INVAL);
    }
    req = (const dsm_alloc_req_t *)msg->data;
    memset(&reply, 0, sizeof(reply));
    st = home_alloc_object(co, msg->src_node, req->seq, req->size,
                           req->tag, &reply);
    if (st != DSM_OK) {
        return send_error(co, msg, st);
    }
    return send_payload(co, msg, DSM_MSG_OBJECT_ALLOC_REPLY, &reply,
                        (uint32_t)sizeof(reply), DSM_OK);
}

/* BARRIER: cuenta llegadas y publica la ronda.                           */
static uint32_t barrier_arrive(dsm_coherence_t *co, uint32_t node, uint32_t round)
{
    uint32_t published;

    if (node >= DSM_MAX_NODES) {
        return atomic_load(&co->barrier_round);
    }
    if (dsm_mutex_lock(&co->barrier_lock) != DSM_OK) {
        return atomic_load(&co->barrier_round);
    }
    if (co->barrier_registered_round[node] != round + 1u) {
        /* +1 porque 0 significa "nunca registrado" y las rondas empiezan en 0. */
        co->barrier_registered_round[node] = round + 1u;
        atomic_fetch_add(&co->barrier_arrived, 1u);
    }
    if (atomic_load(&co->barrier_arrived) >= co->n_nodes) {
        atomic_store(&co->barrier_arrived, 0u);
        atomic_fetch_add(&co->barrier_round, 1u);
        stats_inc(co->stats, DSM_STAT_BARRIERS);
    }
    published = atomic_load(&co->barrier_round);
    dsm_mutex_unlock(&co->barrier_lock);
    return published;
}

static dsm_status_t handle_barrier(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    const dsm_barrier_req_t *req;
    dsm_barrier_reply_t      reply;

    if (co->node_id != co->home_node) {
        return send_error(co, msg, DSM_ERR_NOTSUP);
    }
    if (msg->data == NULL || msg->data_len < sizeof(dsm_barrier_req_t)) {
        return send_error(co, msg, DSM_ERR_INVAL);
    }
    req = (const dsm_barrier_req_t *)msg->data;
    memset(&reply, 0, sizeof(reply));
    reply.round   = barrier_arrive(co, msg->src_node, req->round);
    reply.arrived = atomic_load(&co->barrier_arrived);
    reply.n_nodes = co->n_nodes;
    reply.status  = (uint32_t)DSM_OK;
    return send_payload(co, msg, DSM_MSG_BARRIER_REPLY, &reply,
                        (uint32_t)sizeof(reply), DSM_OK);
}

/* PAGE_LIST: volcado de la tabla local (lo usa la web y dsmctl).         */
static dsm_status_t handle_page_list(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_info_t *infos;
    uint32_t         cap = 256u;
    uint32_t         n = 0;
    uint32_t         b;

    infos = calloc(cap, sizeof(*infos));
    if (infos == NULL) {
        return send_error(co, msg, DSM_ERR_NOMEM);
    }
    if (dsm_mutex_lock(&co->mem->table_lock) == DSM_OK) {
        for (b = 0; b < co->mem->bucket_count && n < cap; b++) {
            dsm_page_entry_t *e;

            for (e = co->mem->buckets[b]; e != NULL && n < cap; e = e->next) {
                if (dsm_mutex_lock(&e->lock) != DSM_OK) {
                    continue;
                }
                infos[n].page_id    = e->meta.page_id;
                infos[n].object_id  = e->meta.object_id;
                infos[n].version    = e->meta.version;
                infos[n].frame_slot = e->meta.frame_slot;
                infos[n].home_node  = co->home_node;
                infos[n].owner_node = co->node_id;
                infos[n].msi_state  = e->meta.msi_state;
                infos[n].residency  = e->meta.residency;
                infos[n].is_dirty   = e->meta.is_dirty;
                infos[n].sharers    = 0;
                dsm_mutex_unlock(&e->lock);
                n++;
            }
        }
        dsm_mutex_unlock(&co->mem->table_lock);
    }

    {
        dsm_status_t st = send_payload(co, msg, DSM_MSG_PAGE_LIST_REPLY, infos,
                                       n * (uint32_t)sizeof(*infos), DSM_OK);

        free(infos);
        return st;
    }
}

dsm_status_t dsm_coherence_handle_message(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    if (co == NULL || msg == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }
    /* El nodo emisor viene de la red: se valida antes de usarlo en un shift. */
    if (msg->src_node >= DSM_MAX_NODES) {
        return DSM_ERR_INVAL;
    }

    switch (msg->type) {
    case DSM_MSG_PAGE_REQUEST:
        return handle_page_request(co, msg);
    case DSM_MSG_OWNERSHIP_REQUEST:
        return handle_ownership_request(co, msg);
    case DSM_MSG_PAGE_FORWARD:
        return handle_page_forward(co, msg);
    case DSM_MSG_INVALIDATE:
        return handle_invalidate(co, msg);
    case DSM_MSG_PAGE_WRITEBACK:
        return handle_writeback(co, msg);
    case DSM_MSG_PAGE_RELEASE:
        return handle_page_release(co, msg);
    case DSM_MSG_OBJECT_ALLOC:
        return handle_object_alloc(co, msg);
    case DSM_MSG_BARRIER:
        return handle_barrier(co, msg);
    case DSM_MSG_PAGE_LIST:
        return handle_page_list(co, msg);
    default:
        /*
         * PAGE_DATA, OWNERSHIP_GRANT, INVALIDATE_ACK, PAGE_LIST_REPLY y
         * OBJECT_ALLOC_REPLY son respuestas de un RPC: las consume la capa de
         * red y no deberían llegar hasta aquí.
         */
        return DSM_ERR_NOTSUP;
    }
}

/* --- API de objetos ----------------------------------------------------- */
dsm_status_t dsm_coherence_alloc(dsm_coherence_t *co, uint64_t size, uint32_t tag,
                                 dsm_alloc_reply_t *out)
{
    dsm_alloc_req_t req;
    dsm_msg_t       msg;
    dsm_msg_t       resp;
    uint32_t        seq;
    dsm_status_t    st;

    if (co == NULL || out == NULL || !co->initialized || size == 0) {
        return DSM_ERR_INVAL;
    }
    memset(out, 0, sizeof(*out));

    /* La secuencia la lleva cada nodo por su cuenta: es el número de llamada
     * a dsm_malloc. Como todos ejecutan el mismo programa, coinciden.      */
    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    seq = (uint32_t)co->alloc_seq;
    co->alloc_seq++;
    dsm_mutex_unlock(&co->obj_lock);

    if (co->node_id == co->home_node) {
        st = home_alloc_object(co, co->node_id, seq, size, tag, out);
        if (st != DSM_OK) {
            return st;
        }
        (void)object_record(co, out, tag, co->node_id);
        return DSM_OK;
    }

    memset(&req, 0, sizeof(req));
    req.size   = size;
    req.seq    = seq;
    req.flags  = 0;
    req.tag    = tag;
    memset(&msg, 0, sizeof(msg));
    msg.type       = DSM_MSG_OBJECT_ALLOC;
    msg.src_node   = co->node_id;
    msg.dst_node   = co->home_node;
    msg.request_id = next_request_id();
    msg.data       = (const uint8_t *)&req;
    msg.data_len   = (uint32_t)sizeof(req);

    st = call_msg(co, &msg, &resp, co->timeout_ms);
    if (st != DSM_OK) {
        return st;
    }
    if (resp.type != DSM_MSG_OBJECT_ALLOC_REPLY ||
        resp.data_len < sizeof(dsm_alloc_reply_t)) {
        if (resp.data != NULL) {
            free((void *)resp.data);
        }
        return DSM_ERR_PROTOCOL;
    }
    memcpy(out, resp.data, sizeof(*out));
    free((void *)resp.data);

    if (out->status != (uint32_t)DSM_OK) {
        return (dsm_status_t)out->status;
    }
    st = object_record(co, out, tag, co->node_id);
    return (st == DSM_ERR_EXIST) ? DSM_OK : st;
}

dsm_status_t dsm_coherence_find_object(dsm_coherence_t *co, uint64_t object_id,
                                       uint32_t tag, dsm_object_t *out)
{
    dsm_object_t *o;

    if (co == NULL || out == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }
    if (object_id == 0 && tag == 0) {
        return DSM_ERR_INVAL;
    }
    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    o = object_find_locked(co, object_id, tag);
    if (o == NULL) {
        dsm_mutex_unlock(&co->obj_lock);
        return DSM_ERR_NOENT;
    }
    *out = *o;
    dsm_mutex_unlock(&co->obj_lock);
    return DSM_OK;
}

/* --- API de barrera ----------------------------------------------------- */
dsm_status_t dsm_coherence_barrier(dsm_coherence_t *co, uint32_t timeout_ms)
{
    uint32_t     my_round;
    uint32_t     deadline;
    uint32_t     limit = (timeout_ms == 0) ? co->timeout_ms : timeout_ms;

    if (co == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }
    if (co->n_nodes <= 1u) {
        return DSM_OK;
    }
    my_round = atomic_load(&co->barrier_round);
    deadline = (uint32_t)dsm_now_ms() + limit;

    for (;;) {
        uint32_t published;

        if (co->node_id == co->home_node) {
            published = barrier_arrive(co, co->node_id, my_round);
        } else {
            dsm_barrier_req_t   req;
            dsm_barrier_reply_t reply;
            dsm_msg_t           msg;
            dsm_msg_t           resp;
            dsm_status_t        st;

            memset(&req, 0, sizeof(req));
            req.round   = my_round;
            req.n_nodes = co->n_nodes;
            memset(&msg, 0, sizeof(msg));
            msg.type       = DSM_MSG_BARRIER;
            msg.src_node   = co->node_id;
            msg.dst_node   = co->home_node;
            msg.request_id = next_request_id();
            msg.data       = (const uint8_t *)&req;
            msg.data_len   = (uint32_t)sizeof(req);

            st = call_msg(co, &msg, &resp, co->timeout_ms);
            if (st != DSM_OK) {
                if (resp.data != NULL) {
                    free((void *)resp.data);
                }
                return st;
            }
            if (resp.type != DSM_MSG_BARRIER_REPLY ||
                resp.data_len < sizeof(reply)) {
                if (resp.data != NULL) {
                    free((void *)resp.data);
                }
                return DSM_ERR_PROTOCOL;
            }
            memcpy(&reply, resp.data, sizeof(reply));
            free((void *)resp.data);
            published = reply.round;
        }

        if (published != my_round) {
            atomic_store(&co->barrier_round, published);
            return DSM_OK;
        }
        if ((uint32_t)dsm_now_ms() >= deadline) {
            stats_inc(co->stats, DSM_STAT_TIMEOUTS);
            return DSM_ERR_TIMEOUT;
        }
        {
            struct timespec ts;

            ts.tv_sec  = 0;
            ts.tv_nsec = (long)DSM_BARRIER_POLL_MS * 1000000L;
            nanosleep(&ts, NULL);
        }
    }
}

/* --- Consulta y volcados ------------------------------------------------ */
dsm_status_t dsm_coherence_snapshot(dsm_coherence_t *co, uint64_t page_id,
                                    dsm_page_snapshot_t *out)
{
    dsm_page_entry_t *page;
    dsm_dir_entry_t  *dir;
    uint64_t          sharers = 0;
    uint32_t          owner = DSM_INVALID_NODE;

    if (co == NULL || out == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }
    page = dsm_memory_page_lookup(co->mem, page_id);
    if (page == NULL) {
        return DSM_ERR_NOENT;
    }

    if (dsm_mutex_lock(&co->dir_lock) == DSM_OK) {
        dir = dir_find_locked(co, page_id);
        if (dir != NULL && dsm_mutex_lock(&dir->op_lock) == DSM_OK) {
            sharers = dir->meta.sharers;
            owner   = dir->meta.owner_node;
            dsm_mutex_unlock(&dir->op_lock);
        }
        dsm_mutex_unlock(&co->dir_lock);
    }

    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    memset(out, 0, sizeof(*out));
    out->page_id    = page->meta.page_id;
    out->version    = page->meta.version;
    out->frame_slot = page->meta.frame_slot;
    out->home_node  = co->home_node;
    out->owner_node = (owner == DSM_INVALID_NODE) ? co->home_node : owner;
    out->sharers    = sharers;
    out->msi_state  = page->meta.msi_state;
    out->residency  = page->meta.residency;
    out->is_dirty   = page->meta.is_dirty;
    dsm_mutex_unlock(&page->lock);
    return DSM_OK;
}

int dsm_coherence_format_pages(dsm_coherence_t *co, char *buf, size_t len)
{
    size_t   used = 0;
    uint32_t b;
    uint32_t count = 0;

    if (co == NULL || buf == NULL || len == 0) {
        return -1;
    }
    used += (size_t)snprintf(buf + used, len - used,
                             "%-10s %-8s %-6s %-10s %-5s %s\n",
                             "pagina", "objeto", "marco", "residencia",
                             "msi", "version");
    if (used >= len) {
        return -1;
    }
    if (dsm_mutex_lock(&co->mem->table_lock) != DSM_OK) {
        return -1;
    }
    for (b = 0; b < co->mem->bucket_count; b++) {
        dsm_page_entry_t *e;

        for (e = co->mem->buckets[b]; e != NULL; e = e->next) {
            int n;

            if (dsm_mutex_lock(&e->lock) != DSM_OK) {
                continue;
            }
            n = snprintf(buf + used, len - used,
                         "%-10llu %-8llu %-6u %-10s %-5s %llu\n",
                         (unsigned long long)e->meta.page_id,
                         (unsigned long long)e->meta.object_id,
                         e->meta.frame_slot,
                         dsm_residency_name((dsm_residency_t)e->meta.residency),
                         dsm_msi_state_name((dsm_msi_state_t)e->meta.msi_state),
                         (unsigned long long)e->meta.version);
            dsm_mutex_unlock(&e->lock);
            if (n < 0 || (size_t)n >= len - used) {
                dsm_mutex_unlock(&co->mem->table_lock);
                return -1;
            }
            used += (size_t)n;
            count++;
        }
    }
    dsm_mutex_unlock(&co->mem->table_lock);
    if (count == 0) {
        used += (size_t)snprintf(buf + used, len - used,
                                 "(sin paginas locales)\n");
    }
    return (int)used;
}

int dsm_coherence_format_dir(dsm_coherence_t *co, char *buf, size_t len)
{
    size_t   used = 0;
    uint32_t b;
    uint32_t count = 0;

    if (co == NULL || buf == NULL || len == 0) {
        return -1;
    }
    used += (size_t)snprintf(buf + used, len - used,
                             "%-10s %-8s %-6s %-5s %-10s %s\n",
                             "pagina", "dueno", "msi", "ver", "sharers", "objeto");
    if (used >= len) {
        return -1;
    }
    if (dsm_mutex_lock(&co->dir_lock) != DSM_OK) {
        return -1;
    }
    for (b = 0; b < co->dir_buckets; b++) {
        dsm_dir_entry_t *e;

        for (e = co->dir[b]; e != NULL; e = e->next) {
            int n;

            if (dsm_mutex_lock(&e->op_lock) != DSM_OK) {
                continue;
            }
            n = snprintf(buf + used, len - used,
                         "%-10llu %-8u %-6s %-5llu 0x%016llx %llu\n",
                         (unsigned long long)e->meta.page_id,
                         e->meta.owner_node,
                         dsm_msi_state_name((dsm_msi_state_t)e->meta.state),
                         (unsigned long long)e->meta.version,
                         (unsigned long long)e->meta.sharers,
                         (unsigned long long)e->meta.object_id);
            dsm_mutex_unlock(&e->op_lock);
            if (n < 0 || (size_t)n >= len - used) {
                dsm_mutex_unlock(&co->dir_lock);
                return -1;
            }
            used += (size_t)n;
            count++;
        }
    }
    dsm_mutex_unlock(&co->dir_lock);
    if (count == 0) {
        used += (size_t)snprintf(buf + used, len - used,
                                 "(directorio vacio: este nodo no es el home)\n");
    }
    return (int)used;
}

int dsm_coherence_format_objects(dsm_coherence_t *co, char *buf, size_t len)
{
    size_t   used = 0;
    uint32_t i;

    if (co == NULL || buf == NULL || len == 0) {
        return -1;
    }
    used += (size_t)snprintf(buf + used, len - used,
                             "%-6s %-10s %-6s %-12s %-6s %s\n",
                             "id", "pag_base", "paginas", "bytes", "dueno", "tag");
    if (used >= len) {
        return -1;
    }
    if (dsm_mutex_lock(&co->obj_lock) != DSM_OK) {
        return -1;
    }
    for (i = 0; i < co->object_count; i++) {
        int n = snprintf(buf + used, len - used,
                         "%-6llu %-10llu %-6u %-12llu %-6u %u\n",
                         (unsigned long long)co->objects[i].object_id,
                         (unsigned long long)co->objects[i].page_base,
                         co->objects[i].page_count,
                         (unsigned long long)co->objects[i].size,
                         co->objects[i].owner_node,
                         co->objects[i].tag);
        if (n < 0 || (size_t)n >= len - used) {
            dsm_mutex_unlock(&co->obj_lock);
            return -1;
        }
        used += (size_t)n;
    }
    dsm_mutex_unlock(&co->obj_lock);
    if (co->object_count == 0) {
        used += (size_t)snprintf(buf + used, len - used,
                                 "(sin objetos asignados)\n");
    }
    return (int)used;
}
