/*
 * dsm_runtime.h — Estado de un nodo DSM y arranque de sus servicios.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 */
#ifndef DSM_RUNTIME_H
#define DSM_RUNTIME_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "dsm_api.h"
#include "dsm_coherence.h"
#include "dsm_config.h"
#include "dsm_fault.h"
#include "dsm_memory.h"
#include "dsm_net.h"
#include "dsm_nodes.h"
#include "dsm_stats.h"

struct dsm_control;
struct dsm_web;
struct dsm_heartbeat;

struct dsm_runtime {
    dsm_config_t cfg;
    dsm_nodes_t  nodes;

    dsm_memory_t    mem;
    dsm_stats_t     stats;
    dsm_coherence_t co;
    dsm_net_t      *net;

    struct dsm_control   *ctl;
    struct dsm_web       *web;
    struct dsm_heartbeat *hb;

    pthread_t        resolver_thread;
    _Atomic int      running;
    uint32_t         n_nodes;
    uint64_t         start_ms;
};

dsm_status_t dsm_runtime_init(dsm_runtime_t *rt, const dsm_config_t *cfg,
                              const dsm_nodes_t *nodes);
void         dsm_runtime_destroy(dsm_runtime_t *rt);

void *dsm_resolver_loop(void *arg);

/* Levanta control, web y heartbeat según la configuración. */
dsm_status_t dsm_runtime_start_services(dsm_runtime_t *rt);

/* Actualiza uptime, residentes y marcos libres antes de imprimir. */
void dsm_runtime_refresh_stats(dsm_runtime_t *rt);
/* Imprime el resumen de contadores del nodo. */
void dsm_runtime_print_stats(dsm_runtime_t *rt, FILE *out);

/* Demostración de página compartida del prototipo 1 (se conserva). */
int dsm_demo_page_run(dsm_runtime_t *rt);

#endif /* DSM_RUNTIME_H */
