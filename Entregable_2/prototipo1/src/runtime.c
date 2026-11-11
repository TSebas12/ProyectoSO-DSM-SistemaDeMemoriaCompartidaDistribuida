#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsm_fault.h"
#include "dsm_memory.h"
#include "dsm_runtime.h"
#include "dsm_sync.h"

static void resolver_loop_body(dsm_runtime_t *rt) {
    for (;;) {
        dsm_fault_req_t req;
        if (dsm_fault_queue_pop(&req, (uint32_t)-1) != DSM_OK) break;
        if (dsm_coherence_handle_fault(&rt->co, &req) != DSM_OK) {
            dsm_fault_mark_failed(req.ctx);
        }
        dsm_fault_wake(req.ctx);
    }
}

void *dsm_resolver_loop(void *arg) {
    dsm_runtime_t *rt = arg;
    resolver_loop_body(rt);
    return NULL;
}

dsm_status_t dsm_runtime_init(dsm_runtime_t *rt, const dsm_config_t *cfg) {
    if (!rt || !cfg) return DSM_ERR_INVAL;
    memset(rt, 0, sizeof(*rt));
    rt->cfg = *cfg;
    /* memory params */
    dsm_memory_params_t mp; dsm_memory_params_default(&mp);
    mp.window_size = cfg->window_size;
    mp.page_size = cfg->page_size;
    mp.frame_count = (uint32_t)(cfg->node_memory / cfg->page_size);
    mp.page_base_id = 0;
    if (dsm_memory_init(&rt->mem, &mp) != DSM_OK) return DSM_ERR_MMAP;
    /* stats */
    /* stats is embedded; assume init zero */
    /* fault map */
    dsm_fault_map_t fm;
    fm.window_base = dsm_memory_window_base(&rt->mem);
    fm.window_size = dsm_memory_window_size(&rt->mem);
    fm.page_size = dsm_memory_page_size(&rt->mem);
    fm.page_base_id = dsm_memory_page_base_id(&rt->mem);
    dsm_fault_params_t fp; memset(&fp,0,sizeof(fp));
    fp.queue_size = cfg->fault_queue_size;
    fp.altstack_size = 0;
    if (dsm_fault_install(&fm, &fp) != DSM_OK) return DSM_ERR_SIGACTION;
    /* net */
    rt->net = dsm_net_create(cfg, &rt->co);
    if (!rt->net) return DSM_ERR_NOMEM;
    dsm_coherence_params_t cp; memset(&cp,0,sizeof(cp));
    cp.node_id = cfg->node_id;
    cp.home_node = 0;
    cp.timeout_ms = cfg->network_timeout_ms;
    cp.dir_buckets = 0;
    dsm_coherence_ops_t ops = dsm_net_ops(rt->net);
    if (dsm_coherence_init(&rt->co, &rt->mem, &rt->stats, &cp, &ops) != DSM_OK) return DSM_ERR_STATE;
    if (dsm_net_start(rt->net) != DSM_OK) return DSM_ERR_NET;
    rt->running = 1;
    pthread_create(&rt->resolver_thread, NULL, dsm_resolver_loop, rt);
    return DSM_OK;
}

void dsm_runtime_destroy(dsm_runtime_t *rt) {
    if (!rt) return;
    if (rt->running) {
        dsm_fault_shutdown();
        pthread_join(rt->resolver_thread, NULL);
        rt->running = 0;
    }
    if (rt->net) { dsm_net_destroy(rt->net); rt->net = NULL; }
    dsm_coherence_destroy(&rt->co);
    dsm_fault_uninstall();
    dsm_memory_destroy(&rt->mem);
}