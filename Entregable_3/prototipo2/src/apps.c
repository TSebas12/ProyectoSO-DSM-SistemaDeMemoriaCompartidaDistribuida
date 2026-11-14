/*
 * apps.c — Selección y valores por defecto de las aplicaciones de prueba.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Cada aplicación corre en todos los nodos (modelo SPMD). El despachador solo
 * traduce el nombre y aplica valores por defecto; la lógica vive en app_*.c.
 */
#include "dsm_apps.h"

#include <string.h>

void dsm_app_opts_default(dsm_app_opts_t *opts)
{
    if (opts == NULL) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->size    = 0;       /* 0 = cada aplicacion elige su tamano          */
    opts->pages   = 0;       /* 0 = bench usa 64 paginas y evict 512         */
    opts->iters   = 100;
    opts->threads = 2;
    opts->seed    = 12345u;
    opts->file    = NULL;
    opts->verify  = 1;
}

dsm_app_kind_t dsm_app_parse(const char *name)
{
    if (name == NULL) {
        return DSM_APP_NONE;
    }
    if (strcmp(name, "demo") == 0)     { return DSM_APP_DEMO; }
    if (strcmp(name, "matrices") == 0) { return DSM_APP_MATRICES; }
    if (strcmp(name, "file") == 0)     { return DSM_APP_FILE; }
    if (strcmp(name, "counter") == 0)  { return DSM_APP_COUNTER; }
    if (strcmp(name, "bench") == 0)    { return DSM_APP_BENCH; }
    if (strcmp(name, "evict") == 0)    { return DSM_APP_EVICT; }
    return DSM_APP_NONE;
}

const char *dsm_app_name(dsm_app_kind_t kind)
{
    switch (kind) {
    case DSM_APP_DEMO:     return "demo";
    case DSM_APP_MATRICES: return "matrices";
    case DSM_APP_FILE:     return "file";
    case DSM_APP_COUNTER:  return "counter";
    case DSM_APP_BENCH:    return "bench";
    case DSM_APP_EVICT:    return "evict";
    default:               return "none";
    }
}

int dsm_app_run(dsm_runtime_t *rt, dsm_app_kind_t kind,
                const dsm_app_opts_t *opts)
{
    dsm_app_opts_t local;

    if (rt == NULL) {
        return -1;
    }
    if (opts == NULL) {
        dsm_app_opts_default(&local);
        opts = &local;
    }

    switch (kind) {
    case DSM_APP_MATRICES:
        return dsm_app_matrices_run(rt, opts);
    case DSM_APP_FILE:
        return dsm_app_file_run(rt, opts);
    case DSM_APP_COUNTER:
        return dsm_app_counter_run(rt, opts);
    case DSM_APP_BENCH:
        return dsm_app_bench_run(rt, opts);
    case DSM_APP_EVICT:
        return dsm_app_evict_run(rt, opts);
    case DSM_APP_DEMO:
        return dsm_demo_page_run(rt);
    default:
        return -1;
    }
}
