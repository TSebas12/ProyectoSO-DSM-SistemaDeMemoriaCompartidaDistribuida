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

    /*
     * Contexto del hilo que usa la aplicación. dsm_attach() lo registra en el
     * manejador de SIGSEGV; el resolutor corre en otro hilo y no toca la
     * ventana, así que no necesita contexto propio.
     */
    dsm_thread_ctx_t app_ctx;
    pthread_t resolver_thread;
    int running;

    /* Instante de arranque, para reportar el tiempo activo en los contadores. */
    uint64_t start_ms;
};

void *dsm_resolver_loop(void *arg);
dsm_status_t dsm_runtime_init(dsm_runtime_t *rt, const dsm_config_t *cfg);
void dsm_runtime_destroy(dsm_runtime_t *rt);

/* Demostración de página compartida; la usa dsm_node. */
void dsm_demo_page_run(dsm_runtime_t *rt);

#endif /* DSM_RUNTIME_H */