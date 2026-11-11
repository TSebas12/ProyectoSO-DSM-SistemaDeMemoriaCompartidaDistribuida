#ifndef DSM_RUNTIME_H
#define DSM_RUNTIME_H

#include <pthread.h>

#include "dsm_api.h"
#include "dsm_coherence.h"
#include "dsm_config.h"
#include "dsm_fault.h"
#include "dsm_memory.h"
#include "dsm_net.h"
#include "dsm_stats.h"

struct dsm_runtime {
    dsm_config_t cfg;
    dsm_memory_t mem;
    dsm_stats_t stats;
    dsm_coherence_t co;
    dsm_net_t *net;
    dsm_thread_ctx_t resolver_ctx;
    pthread_t resolver_thread;
    int running;
};

void *dsm_resolver_loop(void *arg);
dsm_status_t dsm_runtime_init(dsm_runtime_t *rt, const dsm_config_t *cfg);
void dsm_runtime_destroy(dsm_runtime_t *rt);

#endif /* DSM_RUNTIME_H */