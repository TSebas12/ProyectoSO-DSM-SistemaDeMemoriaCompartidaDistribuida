/*
 * demo_page.c — Demostración de una página compartida entre dos o más nodos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Secuencia (la misma del prototipo 1, ahora con barreras en vez de sondeo):
 *
 *   1. el nodo 0 escribe la página 0 del objeto,
 *   2. el nodo 1 la lee (I -> S),
 *   3. el nodo 1 la escribe (S -> M; el home invalida al nodo 0),
 *   4. el nodo 0 la vuelve a leer (el home reenvía desde el dueño).
 *
 * La barrera centralizada sustituye a la espera activa del prototipo 1: no hay
 * que adivinar cuánto tarda el otro nodo.
 */
#include <stdio.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_coherence.h"
#include "dsm_memory.h"
#include "dsm_runtime.h"
#include "dsm_stats.h"

#define DSM_DEMO_BARRIER_MS 30000u

int dsm_demo_page_run(dsm_runtime_t *rt)
{
    uint32_t  rank;
    uint32_t  nodes;
    uint32_t  page_size;
    uint8_t  *obj;

    if (rt == NULL) {
        return -1;
    }
    rank      = dsm_node_id(rt);
    nodes     = dsm_node_count(rt);
    page_size = dsm_page_size(rt);

    if (nodes < 2u) {
        printf("nodo %u: demo: se necesitan al menos 2 nodos (hay %u)\n",
               rank, nodes);
        return -1;
    }

    /* Objeto de 1 página (solo se usa la página 0). */
    obj = dsm_malloc(rt, (size_t)page_size);
    if (obj == NULL) {
        fprintf(stderr, "nodo %u: demo: no se pudo reservar el objeto\n", rank);
        return -1;
    }

    /* Solo nodos 0 y 1 participan en la demo; nodos >= 2 solo esperan barreras. */
    if (rank == 0) {
        snprintf((char *)obj, page_size, "hola desde el nodo 0");
        printf("nodo 0: escribio la pagina 0: '%s'\n", (char *)obj);
    }
    (void)dsm_barrier(rt, DSM_DEMO_BARRIER_MS);

    if (rank == 1) {
        printf("nodo 1: leyo la pagina 0: '%s'\n", (char *)obj);
        snprintf((char *)obj, page_size, "hola desde el nodo 1");
        printf("nodo 1: escribio la pagina 0: '%s'\n", (char *)obj);
    }
    (void)dsm_barrier(rt, DSM_DEMO_BARRIER_MS);

    if (rank == 0) {
        printf("nodo 0: tras el writeback, pagina 0 -> '%s'\n", (char *)obj);
    }
    (void)dsm_barrier(rt, DSM_DEMO_BARRIER_MS);

    if (rank == 0) {
        dsm_stats_view_t view;

        dsm_stats(rt, &view);
        printf("nodo 0: demo: fallos=%llu lecturas_remotas=%llu "
               "escrituras_remotas=%llu invalidaciones_enviadas=%llu "
               "reenvios=%llu\n",
               (unsigned long long)view.field[DSM_STAT_PAGE_FAULTS],
               (unsigned long long)view.field[DSM_STAT_REMOTE_READS],
               (unsigned long long)view.field[DSM_STAT_REMOTE_WRITES],
               (unsigned long long)view.field[DSM_STAT_INVALIDATIONS_SENT],
               (unsigned long long)view.field[DSM_STAT_FORWARDINGS]);
    }
    return 0;
}
