/*
 * dsm_apps.h — Aplicaciones de prueba del prototipo 2.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Cada aplicación corre en todos los nodos a la vez (modelo SPMD): todas piden
 * los mismos objetos en el mismo orden y se sincronizan con dsm_barrier().
 */
#ifndef DSM_APPS_H
#define DSM_APPS_H

#include <stdint.h>

#include "dsm_runtime.h"

typedef enum dsm_app_kind {
    DSM_APP_NONE     = 0,
    DSM_APP_DEMO     = 1,   /* demostración de una página (E2)            */
    DSM_APP_MATRICES = 2,   /* multiplicación de matrices por bloques     */
    DSM_APP_FILE     = 3,   /* archivo/video: checksum distribuido        */
    DSM_APP_COUNTER  = 4,   /* incrementos concurrentes (coherencia)      */
    DSM_APP_BENCH    = 5,   /* latencia y throughput de páginas           */
    DSM_APP_EVICT    = 6    /* presión de memoria para forzar LRU         */
} dsm_app_kind_t;

typedef struct dsm_app_opts {
    uint32_t    size;      /* lado de la matriz o bytes del archivo       */
    uint32_t    pages;     /* páginas usadas por bench/evict              */
    uint32_t    iters;     /* iteraciones por hilo                        */
    uint32_t    threads;   /* hilos por nodo                              */
    uint32_t    seed;      /* semilla del generador                       */
    const char *file;      /* archivo de entrada para --app file          */
    int         verify;    /* comparar contra la referencia local         */
} dsm_app_opts_t;

void              dsm_app_opts_default(dsm_app_opts_t *opts);
dsm_app_kind_t    dsm_app_parse(const char *name);
const char       *dsm_app_name(dsm_app_kind_t kind);
/* Devuelve 0 si la aplicación terminó bien. */
int               dsm_app_run(dsm_runtime_t *rt, dsm_app_kind_t kind,
                              const dsm_app_opts_t *opts);

/* Implementaciones; cada una corre en todos los nodos a la vez. */
int dsm_app_matrices_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts);
int dsm_app_file_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts);
int dsm_app_counter_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts);
int dsm_app_bench_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts);
int dsm_app_evict_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts);

#endif /* DSM_APPS_H */
