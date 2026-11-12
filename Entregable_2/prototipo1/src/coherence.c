/*
 * coherence.c — Protocolo MSI, directorio y resolución de fallos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * Regla que ordena todo el archivo: nunca se espera a la red con el lock de
 * una página tomado. El hilo resolutor marca la página como TRAYENDO, suelta
 * el lock, hace el RPC y vuelve a tomarlo para publicar el resultado.
 *
 * El estado de la página se publica en este orden:
 *   1. copiar los datos por el mapeo de respaldo,
 *   2. ajustar los metadatos bajo el lock,
 *   3. aplicar los permisos finales con mprotect.
 * Así ningún hilo de usuario ve datos a medio copiar con permisos de lectura.
 */
#include "dsm_coherence.h"

#include <stdlib.h>
#include <string.h>

#define DSM_DIR_BUCKETS_DEFAULT 256u
#define DSM_TIMEOUT_MS_DEFAULT  3000u

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
    uint32_t b;

    e = dir_find_locked(co, page_id);
    if (e != NULL) {
        return e;
    }
    e = calloc(1, sizeof(*e));
    if (e == NULL) {
        return NULL;
    }
    e->meta.page_id   = page_id;
    e->meta.home_node = co->home_node;
    e->meta.owner_node = co->home_node;
    e->meta.state     = (uint8_t)DSM_PAGE_I;
    b = dir_bucket_of(co, page_id);
    e->next = co->dir[b];
    co->dir[b] = e;
    return e;
}

static void dir_mark_sharer(dsm_coherence_t *co, uint64_t page_id, uint32_t node)
{
    dsm_dir_entry_t *e;

    if (node >= 64u) {
        return;
    }
    if (dsm_mutex_lock(&co->dir_lock) != DSM_OK) {
        return;
    }
    e = dir_get_or_create_locked(co, page_id);
    if (e != NULL) {
        e->meta.sharers |= (1ull << node);
        e->meta.state = (uint8_t)DSM_PAGE_S;
        e->meta.last_update_ms = dsm_now_ms();
    }
    dsm_mutex_unlock(&co->dir_lock);
}

static void dir_mark_owner(dsm_coherence_t *co, uint64_t page_id, uint32_t node)
{
    dsm_dir_entry_t *e;

    if (dsm_mutex_lock(&co->dir_lock) != DSM_OK) {
        return;
    }
    e = dir_get_or_create_locked(co, page_id);
    if (e != NULL) {
        e->meta.sharers = (node < 64u) ? (1ull << node) : 0ull;
        e->meta.owner_node = node;
        e->meta.state = (uint8_t)DSM_PAGE_M;
        e->meta.version++;
        e->meta.last_update_ms = dsm_now_ms();
    }
    dsm_mutex_unlock(&co->dir_lock);
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

    co->mem      = mem;
    co->stats    = stats;
    co->node_id  = (p != NULL) ? p->node_id : 0u;
    co->home_node = (p != NULL) ? p->home_node : 0u;
    /* El nodo se usa como índice de un bitmap de 64 bits en el directorio. */
    if (co->node_id >= DSM_MAX_NODES || co->home_node >= DSM_MAX_NODES) {
        return DSM_ERR_INVAL;
    }
    co->timeout_ms = (p != NULL && p->timeout_ms != 0) ? p->timeout_ms
                                                       : DSM_TIMEOUT_MS_DEFAULT;
    co->dir_buckets = (p != NULL && p->dir_buckets != 0) ? p->dir_buckets
                                                         : DSM_DIR_BUCKETS_DEFAULT;
    if (ops != NULL) {
        co->ops = *ops;
    }

    co->dir = calloc(co->dir_buckets, sizeof(dsm_dir_entry_t *));
    if (co->dir == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_init(&co->dir_lock, DSM_LOCK_GLOBAL, "directorio") != DSM_OK) {
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
            free(e);
            e = next;
        }
        co->dir[i] = NULL;
    }
    free(co->dir);
    co->dir = NULL;
    dsm_mutex_destroy(&co->dir_lock);
    co->initialized = 0;
}

/* --- Traer una página --------------------------------------------------- */
/*
 * Hace el RPC y publica los datos. Se ejecuta sin el lock de la página: la
 * regla del E1 prohíbe esperar a la red con el lock tomado.
 */
static dsm_status_t coherence_fetch(dsm_coherence_t *co, uint64_t page_id,
                                    dsm_msi_event_t ev, dsm_msi_state_t *out_state)
{
    uint32_t     page_size = dsm_memory_page_size(co->mem);
    uint8_t     *buf;
    dsm_status_t st;

    if (ev == DSM_EV_LOCAL_READ) {
        if (co->ops.fetch_shared == NULL) {
            return DSM_ERR_NOTSUP;
        }
    } else if (co->ops.fetch_exclusive == NULL) {
        return DSM_ERR_NOTSUP;
    }

    buf = malloc(page_size);
    if (buf == NULL) {
        return DSM_ERR_NOMEM;
    }

    if (ev == DSM_EV_LOCAL_READ) {
        st = co->ops.fetch_shared(co->ops.ctx, page_id, buf, page_size,
                                  co->timeout_ms);
    } else {
        st = co->ops.fetch_exclusive(co->ops.ctx, page_id, buf, page_size,
                                     co->timeout_ms);
    }

    if (st == DSM_OK) {
        *out_state = (ev == DSM_EV_LOCAL_READ) ? DSM_PAGE_S : DSM_PAGE_M;
        st = dsm_memory_copy_in(co->mem, page_id, buf, page_size);
        if (st == DSM_OK) {
            st = dsm_memory_protect(co->mem, page_id, *out_state);
        }
    }
    free(buf);

    if (st == DSM_OK) {
        dsm_stats_inc(co->stats, (ev == DSM_EV_LOCAL_READ) ? DSM_STAT_REMOTE_READS
                                                           : DSM_STAT_REMOTE_WRITES);
    }
    return st;
}

dsm_status_t dsm_coherence_handle_fault(dsm_coherence_t *co, const dsm_fault_req_t *req)
{
    dsm_page_entry_t *page;
    dsm_msi_state_t   state;
    dsm_msi_state_t   nuevo = DSM_PAGE_I;
    dsm_msi_event_t   ev;
    dsm_status_t      st;
    int               es_lectura;

    if (co == NULL || req == NULL || !co->initialized) {
        return DSM_ERR_INVAL;
    }

    /* Un fallo desconocido se trata como escritura: pedir propiedad siempre
     * es correcto, aunque cueste una invalidación de más. */
    es_lectura = (req->kind == DSM_FAULT_READ);
    ev = es_lectura ? DSM_EV_LOCAL_READ : DSM_EV_LOCAL_WRITE;

    page = dsm_memory_page_get_or_create(co->mem, req->page_id, 0);
    if (page == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }

    /*
     * Coalescencia: si otro hilo ya está trayendo esta página, este hilo
     * espera en la condición en lugar de lanzar un segundo RPC.
     */
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
            state = (dsm_msi_state_t)page->meta.msi_state;
            if ((es_lectura && (state == DSM_PAGE_S || state == DSM_PAGE_M)) ||
                (!es_lectura && state == DSM_PAGE_M)) {
                dsm_stats_inc(co->stats, DSM_STAT_LOCAL_HITS);
                dsm_mutex_unlock(&page->lock);
                return DSM_OK;
            }
        }
        /*
         * El home no puede pedirse una página a sí mismo: si ya la entregó a
         * otro nodo, haría falta PAGE_FORWARD (E3). Se devuelve un error claro
         * en lugar de abrir un RPC contra el propio listener.
         */
        if (co->node_id == co->home_node && res == DSM_RES_NOT_RESIDENT) {
            dsm_mutex_unlock(&page->lock);
            return DSM_ERR_NOENT;
        }
        break;
    }

    page->meta.residency = (uint8_t)DSM_RES_FETCHING;
    dsm_mutex_unlock(&page->lock);

    st = coherence_fetch(co, req->page_id, ev, &nuevo);

    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    if (st == DSM_OK) {
        page->meta.msi_state = (uint8_t)nuevo;
        page->meta.residency = (uint8_t)DSM_RES_RESIDENT;
        page->meta.is_dirty  = (nuevo == DSM_PAGE_M) ? 1u : 0u;
        page->meta.version++;
        if (page->meta.frame_slot == DSM_INVALID_FRAME) {
            page->meta.frame_slot = dsm_memory_alloc_frame(co->mem, req->page_id);
        }
        dsm_stats_inc(co->stats, DSM_STAT_PAGE_FAULTS);
    } else {
        page->meta.residency = (uint8_t)DSM_RES_ERROR;
        dsm_stats_inc(co->stats, DSM_STAT_ERRORS);
    }
    dsm_cond_broadcast(&page->cond);
    dsm_mutex_unlock(&page->lock);

    return st;
}

/* --- Lado servidor ------------------------------------------------------ */
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

/*
 * OWNERSHIP_DENY es una respuesta del protocolo, no un error de transporte: el
 * solicitante debe saber que el home no puede conceder la propiedad porque hay
 * otra copia compartida. El E3 añadirá la invalidación de ese tercero.
 */
static dsm_status_t send_deny(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    return send_status(co, msg, DSM_MSG_OWNERSHIP_DENY, DSM_ERR_NOTSUP);
}

static dsm_status_t send_page(dsm_coherence_t *co, const dsm_msg_t *msg,
                              uint16_t type, uint8_t *buf, uint32_t len)
{
    dsm_msg_t resp;

    if (co->ops.send == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(&resp, 0, sizeof(resp));
    resp.type       = type;
    resp.flags      = (uint16_t)(DSM_FLAG_RESPONSE | DSM_FLAG_HAS_DATA);
    resp.src_node   = co->node_id;
    resp.dst_node   = msg->src_node;
    resp.request_id = msg->request_id;
    resp.page_id    = msg->page_id;
    resp.status     = (int32_t)DSM_OK;
    resp.data       = buf;
    resp.data_len   = len;
    return co->ops.send(co->ops.ctx, &resp);
}

/* PAGE_REQUEST: el home entrega una copia de solo lectura.               */
static dsm_status_t serve_page_request(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    dsm_msi_state_t   state;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    uint8_t          *buf;
    dsm_status_t      st;

    page = dsm_memory_page_lookup(co->mem, msg->page_id);
    if (page == NULL) {
        return send_error(co, msg, DSM_ERR_NOENT);
    }
    buf = malloc(page_size);
    if (buf == NULL) {
        return DSM_ERR_NOMEM;
    }

    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        free(buf);
        return DSM_ERR_STATE;
    }
    if (page->meta.residency != (uint8_t)DSM_RES_RESIDENT) {
        dsm_mutex_unlock(&page->lock);
        free(buf);
        return send_error(co, msg, DSM_ERR_NOENT);
    }

    /* M -> S: el home se queda con una copia limpia y compartida. */
    state = (dsm_msi_state_t)page->meta.msi_state;
    if (state == DSM_PAGE_M) {
        state = (dsm_msi_state_t)dsm_msi_next_state(state, DSM_EV_REMOTE_READ);
        page->meta.msi_state = (uint8_t)state;
        page->meta.is_dirty  = 0;
    }
    st = dsm_memory_copy_out(co->mem, msg->page_id, buf, page_size);
    /* copy_out lee por el respaldo y no toca permisos; aun así hay que
     * revocar la escritura cuando el home baja de M a S. */
    dsm_memory_protect(co->mem, msg->page_id, state);
    page->meta.version++;
    dsm_mutex_unlock(&page->lock);

    if (st == DSM_OK) {
        st = send_page(co, msg, DSM_MSG_PAGE_DATA, buf, page_size);
    }
    free(buf);

    if (st == DSM_OK) {
        dir_mark_sharer(co, msg->page_id, msg->src_node);
        dsm_stats_inc(co->stats, DSM_STAT_REMOTE_READS);
    }
    return st;
}

/* OWNERSHIP_REQUEST: el home concede la propiedad si no hay terceros.    */
static dsm_status_t serve_ownership_request(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    dsm_dir_entry_t  *dir;
    uint64_t          otros;
    uint32_t          page_size = dsm_memory_page_size(co->mem);
    uint8_t          *buf;
    dsm_status_t      st;

    page = dsm_memory_page_lookup(co->mem, msg->page_id);
    if (page == NULL) {
        return send_error(co, msg, DSM_ERR_NOENT);
    }

    /*
     * Con dos nodos el home siempre puede conceder: el único sharer posible
     * es el propio solicitante. Si hubiera un tercero habría que invalidarlo
     * y esperar su ACK; eso queda para el E3 y se rechaza explícitamente.
     */
    if (dsm_mutex_lock(&co->dir_lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }
    dir = dir_find_locked(co, msg->page_id);
    otros = (dir != NULL) ? dir->meta.sharers : 0ull;
    dsm_mutex_unlock(&co->dir_lock);
    otros &= ~(1ull << msg->src_node);
    otros &= ~(1ull << co->node_id);
    if (otros != 0ull) {
        return send_deny(co, msg);
    }

    buf = malloc(page_size);
    if (buf == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        free(buf);
        return DSM_ERR_STATE;
    }
    if (page->meta.residency != (uint8_t)DSM_RES_RESIDENT) {
        dsm_mutex_unlock(&page->lock);
        free(buf);
        return send_error(co, msg, DSM_ERR_NOENT);
    }

    st = dsm_memory_copy_out(co->mem, msg->page_id, buf, page_size);

    /* El home entrega la única copia: pasa a I y libera el marco. */
    page->meta.msi_state = (uint8_t)DSM_PAGE_I;
    page->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
    page->meta.is_dirty  = 0;
    page->meta.version++;
    dsm_memory_protect(co->mem, msg->page_id, DSM_PAGE_I);
    if (page->meta.frame_slot != DSM_INVALID_FRAME) {
        dsm_memory_release_frame(co->mem, page->meta.frame_slot);
        page->meta.frame_slot = DSM_INVALID_FRAME;
    }
    dsm_mutex_unlock(&page->lock);

    if (st == DSM_OK) {
        st = send_page(co, msg, DSM_MSG_OWNERSHIP_GRANT, buf, page_size);
    }
    free(buf);

    if (st == DSM_OK) {
        dir_mark_owner(co, msg->page_id, msg->src_node);
    }
    return st;
}

/* INVALIDATE: el nodo descarta su copia y confirma.                      */
static dsm_status_t handle_invalidate(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    dsm_msg_t         ack;

    page = dsm_memory_page_lookup(co->mem, msg->page_id);
    if (page != NULL && dsm_mutex_lock(&page->lock) == DSM_OK) {
        if (page->meta.residency == (uint8_t)DSM_RES_RESIDENT &&
            page->meta.msi_state != (uint8_t)DSM_PAGE_I) {
            page->meta.msi_state = (uint8_t)DSM_PAGE_I;
            page->meta.residency = (uint8_t)DSM_RES_NOT_RESIDENT;
            page->meta.is_dirty  = 0;
            page->meta.version++;
            dsm_memory_protect(co->mem, msg->page_id, DSM_PAGE_I);
            if (page->meta.frame_slot != DSM_INVALID_FRAME) {
                dsm_memory_release_frame(co->mem, page->meta.frame_slot);
                page->meta.frame_slot = DSM_INVALID_FRAME;
            }
            dsm_stats_inc(co->stats, DSM_STAT_INVALIDATIONS_RECEIVED);
        }
        dsm_mutex_unlock(&page->lock);
    }

    if (co->ops.send == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(&ack, 0, sizeof(ack));
    ack.type       = DSM_MSG_INVALIDATE_ACK;
    ack.flags      = DSM_FLAG_RESPONSE;
    ack.src_node   = co->node_id;
    ack.dst_node   = msg->src_node;
    ack.request_id = msg->request_id;
    ack.page_id    = msg->page_id;
    ack.status     = (int32_t)DSM_OK;
    return co->ops.send(co->ops.ctx, &ack);
}

/* PAGE_WRITEBACK: el home recibe de vuelta una página sucia.             */
static dsm_status_t handle_writeback(dsm_coherence_t *co, const dsm_msg_t *msg)
{
    dsm_page_entry_t *page;
    dsm_msg_t         ack;
    dsm_status_t      st;

    if (msg->data == NULL || msg->data_len != dsm_memory_page_size(co->mem)) {
        return send_error(co, msg, DSM_ERR_INVAL);
    }
    page = dsm_memory_page_get_or_create(co->mem, msg->page_id, 0);
    if (page == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_lock(&page->lock) != DSM_OK) {
        return DSM_ERR_STATE;
    }

    st = dsm_memory_copy_in(co->mem, msg->page_id, msg->data, msg->data_len);
    if (st != DSM_OK) {
        dsm_mutex_unlock(&page->lock);
        return st;
    }
    page->meta.msi_state = (uint8_t)DSM_PAGE_S;
    page->meta.residency = (uint8_t)DSM_RES_RESIDENT;
    page->meta.is_dirty  = 0;
    page->meta.version++;
    if (page->meta.frame_slot == DSM_INVALID_FRAME) {
        page->meta.frame_slot = dsm_memory_alloc_frame(co->mem, msg->page_id);
    }
    dsm_memory_protect(co->mem, msg->page_id, DSM_PAGE_S);
    dsm_mutex_unlock(&page->lock);

    /* El home vuelve a tener una copia limpia y compartida con el emisor. */
    dir_mark_sharer(co, msg->page_id, msg->src_node);

    if (co->ops.send == NULL) {
        return DSM_ERR_NOTSUP;
    }
    memset(&ack, 0, sizeof(ack));
    ack.type       = DSM_MSG_PAGE_WRITEBACK_ACK;
    ack.flags      = DSM_FLAG_RESPONSE;
    ack.src_node   = co->node_id;
    ack.dst_node   = msg->src_node;
    ack.request_id = msg->request_id;
    ack.page_id    = msg->page_id;
    ack.status     = (int32_t)DSM_OK;
    return co->ops.send(co->ops.ctx, &ack);
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
        return serve_page_request(co, msg);
    case DSM_MSG_OWNERSHIP_REQUEST:
        return serve_ownership_request(co, msg);
    case DSM_MSG_INVALIDATE:
        return handle_invalidate(co, msg);
    case DSM_MSG_PAGE_WRITEBACK:
        return handle_writeback(co, msg);
    default:
        /*
         * PAGE_DATA, OWNERSHIP_GRANT e INVALIDATE_ACK son respuestas de un RPC:
         * las consume la capa de red y no deberían llegar hasta aquí.
         */
        return DSM_ERR_NOTSUP;
    }
}

/* --- Consulta ----------------------------------------------------------- */
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

    /* El directorio se lee antes de tomar el lock de página para respetar
     * la jerarquía: GLOBAL antes que PAGINA. */
    if (dsm_mutex_lock(&co->dir_lock) == DSM_OK) {
        dir = dir_find_locked(co, page_id);
        if (dir != NULL) {
            sharers = dir->meta.sharers;
            owner = dir->meta.owner_node;
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
