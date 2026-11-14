/*
 * app_bench.c — Latencia de lecturas locales, lecturas remotas y escrituras.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Para que cada operación remota sea realmente remota, el nodo r usa páginas
 * que inicialmente pertenecen al nodo (r+1) % n y no vuelve a tocarlas. Así la
 * primera lectura pide la página al dueño y la primera escritura pide la
 * propiedad; no se mide un acierto local disfrazado de acceso remoto.
 *
 * Se separan tres fases para poder comparar:
 *   1. lectura local   (acierto, sin red)
 *   2. lectura remota  (PAGE_REQUEST + PAGE_FORWARD)
 *   3. escritura remota (OWNERSHIP_REQUEST + INVALIDATE + GRANT)
 */
#include "dsm_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_BARRIER_MS 60000u

static void print_rate(const char *label, uint32_t rank, uint64_t ops,
                       uint64_t total_us, uint64_t bytes)
{
    double us_op = (ops == 0) ? 0.0 : (double)total_us / (double)ops;
    double mb_s  = (total_us == 0) ? 0.0
                  : ((double)bytes / (1024.0 * 1024.0)) /
                    ((double)total_us / 1000000.0);

    printf("nodo %u: bench %-16s %6llu ops, %8.2f us/op, %7.2f MB/s\n",
           rank, label, (unsigned long long)ops, us_op, mb_s);
}

int dsm_app_bench_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t  rank  = dsm_node_id(rt);
    uint32_t  nodes = dsm_node_count(rt);
    uint32_t  page_size = dsm_page_size(rt);
    uint32_t  pages = (opts->pages == 0) ? 64u : opts->pages;
    uint32_t  per_node;
    uint32_t  iters;
    uint8_t  *buf;
    uint64_t  t0;
    uint64_t  t1;
    uint64_t  sink = 0;
    uint32_t  i;

    if (nodes == 0) {
        nodes = 1;
    }
    if (pages < nodes) {
        pages = nodes;
    }
    per_node = pages / nodes;
    iters = (opts->iters == 0) ? per_node : opts->iters;
    if (iters > per_node) {
        iters = per_node;
    }
    if (iters == 0) {
        iters = 1;
    }

    buf = dsm_malloc(rt, (size_t)pages * (size_t)page_size);
    if (buf == NULL) {
        fprintf(stderr, "nodo %u: bench: no se pudo reservar %u paginas\n",
                rank, pages);
        return 1;
    }

    /* Reparto inicial: el nodo r escribe las páginas p con p % nodes == r. */
    for (i = 0; i < pages; i++) {
        if ((i % nodes) == rank) {
            memset(buf + (size_t)i * page_size, (int)(rank + 1u), page_size);
        }
    }
    (void)dsm_barrier(rt, APP_BARRIER_MS);

    /* Fase 1: lectura local de las páginas propias.                      */
    t0 = dsm_app_now_us();
    for (i = 0; i < iters; i++) {
        uint32_t p = (i % per_node) * nodes + rank;

        sink += buf[(size_t)p * page_size + (i % page_size)];
    }
    t1 = dsm_app_now_us();
    print_rate("lectura local", rank, iters, t1 - t0, 0);

    /* Fase 2: lectura de las páginas del nodo siguiente.                 */
    t0 = dsm_app_now_us();
    for (i = 0; i < iters; i++) {
        uint32_t p = (i % per_node) * nodes + ((rank + 1u) % nodes);

        sink += buf[(size_t)p * page_size + (i % page_size)];
    }
    t1 = dsm_app_now_us();
    print_rate("lectura remota", rank, iters, t1 - t0, 0);

    /* Fase 3: escritura de las páginas del nodo siguiente.               */
    t0 = dsm_app_now_us();
    for (i = 0; i < iters; i++) {
        uint32_t p = (i % per_node) * nodes + ((rank + 1u) % nodes);

        buf[(size_t)p * page_size + (i % page_size)] = (uint8_t)(rank + 1u);
    }
    t1 = dsm_app_now_us();
    print_rate("escritura remota", rank, iters, t1 - t0,
               (uint64_t)iters * (uint64_t)page_size);

    (void)dsm_barrier(rt, APP_BARRIER_MS);
    printf("nodo %u: bench: %u paginas, %u operaciones por fase (suma %llu)\n",
           rank, pages, iters, (unsigned long long)sink);
    return 0;
}
