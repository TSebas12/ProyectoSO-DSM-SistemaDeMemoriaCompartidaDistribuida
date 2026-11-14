/*
 * dsm_api.c — API que usan las aplicaciones.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * El prototipo 1 compartía un único dsm_thread_ctx_t entre todos los hilos de
 * la aplicación. Eso funcionaba con un solo hilo, pero en cuanto una aplicación
 * usa varios hilos el manejador de SIGSEGV necesita saber cuál se quedó
 * esperando su página. Aquí cada hilo registra su propio contexto en una clave
 * pthread; el manejador (fault.c) lo lee de una variable _Thread_local.
 *
 * El asignador es determinista, no un malloc real: dsm_malloc pide al home un
 * tramo de páginas globales identificado por la secuencia de la llamada. Todos
 * los nodos ejecutan el mismo programa y piden los mismos tamaños en el mismo
 * orden, así que reciben el mismo page_base. Es el contrato SPMD del proyecto.
 */
#include "dsm_api.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_fault.h"
#include "dsm_memory.h"
#include "dsm_runtime.h"

/* --- Contexto por hilo --------------------------------------------------- */
static pthread_key_t  g_ctx_key;
static pthread_once_t g_ctx_once = PTHREAD_ONCE_INIT;

static void ctx_destructor(void *ptr)
{
    dsm_fault_detach_thread();
    free(ptr);
}

static void ctx_key_create(void)
{
    (void)pthread_key_create(&g_ctx_key, ctx_destructor);
}

dsm_status_t dsm_attach(dsm_runtime_t *rt)
{
    dsm_thread_ctx_t *ctx;

    if (rt == NULL) {
        return DSM_ERR_INVAL;
    }
    (void)pthread_once(&g_ctx_once, ctx_key_create);
    if (pthread_getspecific(g_ctx_key) != NULL) {
        return DSM_OK;   /* ya estaba registrado */
    }
    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_fault_attach_thread(ctx) != DSM_OK) {
        free(ctx);
        return DSM_ERR_STATE;
    }
    if (pthread_setspecific(g_ctx_key, ctx) != 0) {
        dsm_fault_detach_thread();
        free(ctx);
        return DSM_ERR_NOMEM;
    }
    return DSM_OK;
}

void dsm_detach(dsm_runtime_t *rt)
{
    dsm_thread_ctx_t *ctx;

    (void)rt;
    (void)pthread_once(&g_ctx_once, ctx_key_create);
    ctx = pthread_getspecific(g_ctx_key);
    if (ctx == NULL) {
        return;
    }
    dsm_fault_detach_thread();
    pthread_setspecific(g_ctx_key, NULL);
    free(ctx);
}

/* --- Ciclo de vida ------------------------------------------------------- */
dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out)
{
    dsm_status_t st;

    if (cfg == NULL || out == NULL) {
        return DSM_ERR_INVAL;
    }
    *out = NULL;

    /*
     * dsm_init no conoce la tabla de nodos: la carga el binario (node_main) y
     * se la pasa a dsm_runtime_init. Para no romper la firma histórica del
     * API, aquí se construye una tabla trivial de un solo nodo.
     */
    {
        dsm_runtime_t *rt = calloc(1, sizeof(*rt));
        dsm_nodes_t    nodes;

        if (rt == NULL) {
            return DSM_ERR_NOMEM;
        }
        memset(&nodes, 0, sizeof(nodes));
        nodes.self = cfg->node_id;
        nodes.count = 1;
        nodes.base_port = cfg->listen_port;
        nodes.peer[cfg->node_id].node_id = cfg->node_id;
        nodes.peer[cfg->node_id].port    = cfg->listen_port;
        nodes.peer[cfg->node_id].valid   = 1;
        snprintf(nodes.peer[cfg->node_id].host,
                 sizeof(nodes.peer[cfg->node_id].host), "%s",
                 cfg->bind_address[0] ? cfg->bind_address : "127.0.0.1");

        st = dsm_runtime_init(rt, cfg, &nodes);
        if (st != DSM_OK) {
            dsm_runtime_destroy(rt);
            free(rt);
            return st;
        }
        *out = rt;
    }
    return DSM_OK;
}

void dsm_shutdown(dsm_runtime_t *rt)
{
    if (rt == NULL) {
        return;
    }
    dsm_runtime_destroy(rt);
    free(rt);
}

dsm_status_t dsm_create(dsm_runtime_t *rt)
{
    /* En el prototipo 2 los objetos se piden con dsm_malloc().            */
    if (rt == NULL) {
        return DSM_ERR_INVAL;
    }
    return DSM_OK;
}

/* --- Memoria compartida -------------------------------------------------- */
void *dsm_malloc(dsm_runtime_t *rt, size_t size)
{
    dsm_alloc_reply_t reply;
    dsm_status_t      st;
    void             *addr;

    if (rt == NULL || size == 0) {
        return NULL;
    }
    st = dsm_coherence_alloc(&rt->co, (uint64_t)size, 0u, &reply);
    if (st != DSM_OK) {
        return NULL;
    }
    addr = dsm_memory_page_addr(&rt->mem, reply.page_base);
    if (addr == NULL) {
        return NULL;
    }
    return addr;
}

void *dsm_calloc(dsm_runtime_t *rt, size_t count, size_t size)
{
    size_t total;

    if (count != 0 && size > (SIZE_MAX / count)) {
        return NULL;
    }
    total = count * size;
    /*
     * Las páginas de un objeto nuevo se materializan con ceros en el home
     * (home_ensure_data), así que no hace falta memset aquí: el primer acceso
     * trae la página ya limpia.
     */
    return dsm_malloc(rt, total);
}

void dsm_free(dsm_runtime_t *rt, void *ptr)
{
    /*
     * El asignador es monótono: no reutiliza huecos. Liberar sería peligroso
     * porque el rango es global y otro nodo puede seguir usándolo. Se deja la
     * función por compatibilidad con el API y para documentar la decisión.
     */
    (void)rt;
    (void)ptr;
}

uint64_t dsm_addr_to_page(dsm_runtime_t *rt, const void *addr)
{
    if (rt == NULL) {
        return DSM_INVALID_PAGE_ID;
    }
    return dsm_memory_addr_to_page(&rt->mem, addr);
}

/* --- Sincronización ------------------------------------------------------ */
dsm_status_t dsm_barrier(dsm_runtime_t *rt, uint32_t timeout_ms)
{
    if (rt == NULL) {
        return DSM_ERR_INVAL;
    }
    return dsm_coherence_barrier(&rt->co, timeout_ms);
}

/* --- Consulta ------------------------------------------------------------ */
uint32_t dsm_node_id(dsm_runtime_t *rt)
{
    return (rt == NULL) ? 0u : rt->cfg.node_id;
}

uint32_t dsm_node_count(dsm_runtime_t *rt)
{
    return (rt == NULL) ? 0u : rt->n_nodes;
}

uint32_t dsm_page_size(dsm_runtime_t *rt)
{
    return (rt == NULL) ? 0u : dsm_memory_page_size(&rt->mem);
}

void dsm_stats(dsm_runtime_t *rt, dsm_stats_view_t *out)
{
    if (rt == NULL || out == NULL) {
        return;
    }
    /* Refresca los contadores derivados (residentes, marcos libres, swap). */
    dsm_runtime_refresh_stats(rt);
    dsm_stats_snapshot(&rt->stats, out);
}
