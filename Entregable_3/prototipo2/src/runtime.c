/*
 * runtime.c — Estado de un nodo DSM y arranque de sus servicios.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Orden de arranque (importa):
 *
 *   1. memoria   — reserva la ventana y abre el archivo de swap
 *   2. contadores
 *   3. manejador de SIGSEGV
 *   4. coherencia — con las operaciones de red todavía vacías
 *   5. red        — crea el pool de hilos y el aceptador
 *   6. se enlazan las operaciones reales y se levanta el listener
 *   7. hilo resolutor de fallos
 *
 * La coherencia se inicializa antes que la red porque la red guarda un puntero
 * a ella, pero no puede recibir mensajes hasta el paso 6. El apagado va al
 * revés: primero se cierran los servicios, luego la red y por último la
 * coherencia y la memoria.
 */
#include "dsm_runtime.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dsm_control.h"
#include "dsm_fault.h"
#include "dsm_heartbeat.h"
#include "dsm_log.h"
#include "dsm_memory.h"
#include "dsm_sync.h"
#include "dsm_web.h"

static void resolver_loop_body(dsm_runtime_t *rt)
{
    for (;;) {
        dsm_fault_req_t req;

        if (dsm_fault_queue_pop(&req, UINT32_MAX) != DSM_OK) {
            break;
        }
        {
            dsm_status_t st = dsm_coherence_handle_fault(&rt->co, &req);

            if (st != DSM_OK) {
                /*
                 * Un fallo de resolución es fatal para el hilo que lo provocó
                 * (fail-stop). Se registra el motivo antes de despertarlo: sin
                 * esta línea el proceso muere con SIGSEGV y no queda rastro de
                 * por qué el DSM no pudo traer la página.
                 */
                DSM_LOG(DSM_LOG_ERROR,
                        "no se pudo resolver la pagina %llu (estado %d)",
                        (unsigned long long)req.page_id, (int)st);
                dsm_fault_mark_failed(req.ctx);
            }
        }
        dsm_fault_wake(req.ctx);
    }
}

void *dsm_resolver_loop(void *arg)
{
    dsm_runtime_t *rt = arg;

    resolver_loop_body(rt);
    return NULL;
}

static void runtime_open_swap(dsm_runtime_t *rt)
{
    char path[128];

    /* El directorio puede no existir en una copia recién clonada.         */
    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        return;
    }
    snprintf(path, sizeof(path), "logs/swap-node%u.bin", rt->cfg.node_id);
    if (dsm_memory_swap_open(&rt->mem, path) != DSM_OK) {
        DSM_LOG(DSM_LOG_WARN, "no se pudo abrir el swap %s; la expulsion del "
                              "home quedara deshabilitada", path);
    }
}

dsm_status_t dsm_runtime_init(dsm_runtime_t *rt, const dsm_config_t *cfg,
                              const dsm_nodes_t *nodes)
{
    dsm_memory_params_t  mp;
    dsm_fault_map_t      fm;
    dsm_fault_params_t   fp;
    dsm_coherence_params_t cp;

    if (rt == NULL || cfg == NULL || nodes == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(rt, 0, sizeof(*rt));
    rt->cfg    = *cfg;
    rt->nodes  = *nodes;
    rt->n_nodes = nodes->count;
    rt->start_ms = dsm_now_ms();

    /* --- 1. memoria ---------------------------------------------------- */
    dsm_memory_params_default(&mp);
    mp.window_size  = cfg->window_size;
    mp.page_size    = cfg->page_size;
    mp.frame_count  = (uint32_t)(cfg->node_memory / cfg->page_size);
    mp.page_base_id = 0;
    if (mp.frame_count == 0) {
        return DSM_ERR_INVAL;
    }
    if (dsm_memory_init(&rt->mem, &mp) != DSM_OK) {
        return DSM_ERR_MMAP;
    }
    runtime_open_swap(rt);

    /* --- 2. contadores -------------------------------------------------- */
    dsm_stats_init(&rt->stats);

    /* --- 3. manejador de SIGSEGV ---------------------------------------- */
    memset(&fm, 0, sizeof(fm));
    fm.window_base  = dsm_memory_window_base(&rt->mem);
    fm.window_size  = dsm_memory_window_size(&rt->mem);
    fm.page_size    = dsm_memory_page_size(&rt->mem);
    fm.page_base_id = dsm_memory_page_base_id(&rt->mem);
    memset(&fp, 0, sizeof(fp));
    fp.queue_size    = cfg->fault_queue_size;
    fp.altstack_size = 0;
    if (dsm_fault_install(&fm, &fp) != DSM_OK) {
        dsm_memory_destroy(&rt->mem);
        return DSM_ERR_SIGACTION;
    }

    /* --- 4. coherencia --------------------------------------------------- */
    memset(&cp, 0, sizeof(cp));
    cp.node_id    = cfg->node_id;
    cp.home_node  = 0;
    cp.n_nodes    = nodes->count;
    cp.timeout_ms = cfg->network_timeout_ms;
    cp.dir_buckets = 0;
    if (dsm_coherence_init(&rt->co, &rt->mem, &rt->stats, &cp, NULL) != DSM_OK) {
        dsm_fault_uninstall();
        dsm_memory_destroy(&rt->mem);
        return DSM_ERR_STATE;
    }

    /* --- 5. red ---------------------------------------------------------- */
    rt->net = dsm_net_create(cfg, nodes, &rt->co);
    if (rt->net == NULL) {
        dsm_coherence_destroy(&rt->co);
        dsm_fault_uninstall();
        dsm_memory_destroy(&rt->mem);
        return DSM_ERR_NOMEM;
    }
    dsm_net_set_stats(rt->net, &rt->stats);

    /* --- 6. operaciones reales y listener -------------------------------- */
    rt->co.ops = dsm_net_ops(rt->net);
    if (dsm_net_start(rt->net) != DSM_OK) {
        dsm_net_destroy(rt->net);
        rt->net = NULL;
        dsm_coherence_destroy(&rt->co);
        dsm_fault_uninstall();
        dsm_memory_destroy(&rt->mem);
        return DSM_ERR_NET;
    }

    /* --- 7. resolutor ---------------------------------------------------- */
    if (pthread_create(&rt->resolver_thread, NULL, dsm_resolver_loop, rt) != 0) {
        dsm_net_destroy(rt->net);
        rt->net = NULL;
        dsm_coherence_destroy(&rt->co);
        dsm_fault_uninstall();
        dsm_memory_destroy(&rt->mem);
        return DSM_ERR_STATE;
    }
    atomic_store(&rt->running, 1);
    return DSM_OK;
}

dsm_status_t dsm_runtime_start_services(dsm_runtime_t *rt)
{
    if (rt == NULL) {
        return DSM_ERR_INVAL;
    }
    if (rt->cfg.control_port != 0) {
        rt->ctl = dsm_control_start(rt);
        if (rt->ctl == NULL) {
            return DSM_ERR_NET;
        }
    }
    if (rt->cfg.web_enabled != 0 && rt->cfg.web_port != 0) {
        rt->web = dsm_web_start(rt);
        if (rt->web == NULL) {
            return DSM_ERR_NET;
        }
    }
    if (rt->n_nodes > 1u) {
        rt->hb = dsm_heartbeat_start(rt);
        if (rt->hb == NULL) {
            return DSM_ERR_NET;
        }
    }
    return DSM_OK;
}

void dsm_runtime_destroy(dsm_runtime_t *rt)
{
    if (rt == NULL) {
        return;
    }
    if (rt->hb != NULL) {
        dsm_heartbeat_stop(rt->hb);
        rt->hb = NULL;
    }
    if (rt->web != NULL) {
        dsm_web_stop(rt->web);
        rt->web = NULL;
    }
    if (rt->ctl != NULL) {
        dsm_control_stop(rt->ctl);
        rt->ctl = NULL;
    }
    if (atomic_load(&rt->running)) {
        dsm_fault_shutdown();
        pthread_join(rt->resolver_thread, NULL);
        atomic_store(&rt->running, 0);
    }
    if (rt->net != NULL) {
        dsm_net_destroy(rt->net);
        rt->net = NULL;
    }
    dsm_coherence_destroy(&rt->co);
    dsm_fault_uninstall();
    dsm_memory_destroy(&rt->mem);
}

void dsm_runtime_refresh_stats(dsm_runtime_t *rt)
{
    uint64_t resident = 0;
    uint64_t free_frames = 0;

    if (rt == NULL) {
        return;
    }
    dsm_stats_set(&rt->stats, DSM_STAT_UPTIME_MS,
                  dsm_now_ms() - rt->start_ms);
    dsm_memory_usage(&rt->mem, &resident, &free_frames);
    dsm_stats_set(&rt->stats, DSM_STAT_PAGES_RESIDENT, resident);
    dsm_stats_set(&rt->stats, DSM_STAT_FRAMES_FREE, free_frames);
    dsm_stats_set(&rt->stats, DSM_STAT_SWAP_BYTES,
                  atomic_load(&rt->mem.swap_bytes));
}

void dsm_runtime_print_stats(dsm_runtime_t *rt, FILE *out)
{
    char buf[4096];

    if (rt == NULL || out == NULL) {
        return;
    }
    dsm_runtime_refresh_stats(rt);
    if (dsm_stats_format(&rt->stats, buf, sizeof(buf)) > 0) {
        fputs(buf, out);
    }
    fprintf(out, "latencia media de rpc: %llu us\n",
            (unsigned long long)dsm_stats_avg_rpc_us(&rt->stats));
}
