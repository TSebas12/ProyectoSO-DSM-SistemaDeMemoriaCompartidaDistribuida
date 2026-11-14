/*
 * app_evict.c — Presión de memoria para forzar la expulsión LRU.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Se reservan más páginas que marcos tiene el nodo y se recorren todas. Al no
 * caber, el resolutor expulsa: en un cliente devuelve la página al home; en el
 * home la escribe al archivo de swap. La segunda pasada vuelve a traerlas, así
 * que el ejercicio cubre ida y vuelta.
 */
#include "dsm_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_BARRIER_MS 120000u
#define APP_MAX_PAGES  8192u

int dsm_app_evict_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t         rank  = dsm_node_id(rt);
    uint32_t         nodes = dsm_node_count(rt);
    uint32_t         page_size = dsm_page_size(rt);
    uint32_t         pages = (opts->pages == 0) ? 512u : opts->pages;
    uint8_t         *buf;
    dsm_stats_view_t view;
    uint64_t         sink = 0;
    uint32_t         i;

    if (nodes == 0) {
        nodes = 1;
    }
    if (pages > APP_MAX_PAGES) {
        pages = APP_MAX_PAGES;
    }
    if (pages < 8) {
        pages = 8;
    }

    buf = dsm_malloc(rt, (size_t)pages * (size_t)page_size);
    if (buf == NULL) {
        fprintf(stderr, "nodo %u: evict: no se pudo reservar %u paginas\n",
                rank, pages);
        return 1;
    }

    printf("nodo %u: evict: tocando %u paginas (marco de 4 KB, pool limitado)\n",
           rank, pages);

    /*
     * Primera pasada: cada nodo escribe SOLO sus páginas (p % nodes == rank).
     * El reparto es disjunto a propósito: si los cuatro nodos escribieran las
     * mismas páginas, el ejercicio mediría ping-pong de propiedad y no presión
     * de memoria. Con páginas disjuntas cada una tiene un único dueño.
     */
    for (i = rank; i < pages; i += nodes) {
        buf[(size_t)i * page_size] = (uint8_t)((i + rank) & 0xFFu);
    }
    if (dsm_barrier(rt, APP_BARRIER_MS) != DSM_OK) {
        fprintf(stderr, "nodo %u: evict: fallo la barrera tras la escritura\n", rank);
        return 3;
    }
    printf("nodo %u: evict: primera pasada lista\n", rank);

    /*
     * Segunda pasada: todos leen todas. Aquí es donde el nodo necesita más
     * marcos que los que tiene: el resolutor expulsa (swap en el home,
     * liberación en los clientes) y luego vuelve a traer si hace falta.
     */
    for (i = 0; i < pages; i++) {
        sink += buf[(size_t)i * page_size + (i % page_size)];
    }
    if (dsm_barrier(rt, APP_BARRIER_MS) != DSM_OK) {
        fprintf(stderr, "nodo %u: evict: fallo la barrera tras la lectura\n", rank);
        return 3;
    }
    printf("nodo %u: evict: segunda pasada lista\n", rank);

    dsm_stats(rt, &view);
    printf("nodo %u: evict: expulsiones=%llu sucias=%llu swap=%llu bytes "
           "residentes=%llu libres=%llu (suma %llu)\n",
           rank,
           (unsigned long long)view.field[DSM_STAT_EVICTIONS],
           (unsigned long long)view.field[DSM_STAT_EVICTIONS_DIRTY],
           (unsigned long long)view.field[DSM_STAT_SWAP_BYTES],
           (unsigned long long)view.field[DSM_STAT_PAGES_RESIDENT],
           (unsigned long long)view.field[DSM_STAT_FRAMES_FREE],
           (unsigned long long)sink);
    return 0;
}
