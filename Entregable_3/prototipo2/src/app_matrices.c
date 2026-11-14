/*
 * app_matrices.c — Multiplicación de matrices por bloques de filas.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Tres objetos compartidos: A, B y C. El nodo 0 inicializa A y B; todos los
 * nodos leen A y B (copias S) y cada uno escribe un bloque contiguo de filas de
 * C (copias M). La verificación recalcula el bloque propio y lo compara contra
 * C, así que detecta cualquier incoherencia del protocolo.
 */
#include "dsm_apps.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_BARRIER_MS 60000u

int dsm_app_matrices_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t n     = (opts->size == 0) ? 64u : opts->size;
    uint32_t rank  = dsm_node_id(rt);
    uint32_t nodes = dsm_node_count(rt);
    size_t   bytes = (size_t)n * (size_t)n * sizeof(double);
    double  *a;
    double  *b;
    double  *c;
    uint64_t start;
    uint64_t count;
    uint64_t t0;
    uint64_t t1;
    uint64_t i;
    uint64_t j;
    uint64_t k;

    if (nodes == 0) {
        nodes = 1;
    }
    /* Los tres dsm_malloc van en el mismo orden en todos los nodos: es lo que
     * garantiza que reciban el mismo page_base. */
    a = dsm_malloc(rt, bytes);
    b = dsm_malloc(rt, bytes);
    c = dsm_malloc(rt, bytes);
    if (a == NULL || b == NULL || c == NULL) {
        fprintf(stderr, "nodo %u: matrices: no se pudo reservar %zu bytes por matriz\n",
                rank, bytes);
        return 1;
    }

    if (rank == 0) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                a[i * n + j] = (double)((i + j) % 7u + 1u);
                b[i * n + j] = (double)((i * j) % 5u + 1u);
            }
        }
    }
    (void)dsm_barrier(rt, APP_BARRIER_MS);

    start = dsm_app_split_start(n, nodes, rank);
    count = dsm_app_split_count(n, nodes, rank);

    t0 = dsm_app_now_us();
    for (i = start; i < start + count; i++) {
        for (j = 0; j < n; j++) {
            double sum = 0.0;

            for (k = 0; k < n; k++) {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
    t1 = dsm_app_now_us();
    (void)dsm_barrier(rt, APP_BARRIER_MS);

    printf("nodo %u: matrices %ux%u, filas %llu..%llu, %.2f ms\n",
           rank, n, n, (unsigned long long)start,
           (unsigned long long)(start + count),
           (double)(t1 - t0) / 1000.0);

    if (opts->verify) {
        uint64_t bad = 0;

        for (i = start; i < start + count; i++) {
            for (j = 0; j < n; j++) {
                double sum = 0.0;

                for (k = 0; k < n; k++) {
                    sum += a[i * n + k] * b[k * n + j];
                }
                if (fabs(c[i * n + j] - sum) > 1e-9) {
                    bad++;
                }
            }
        }
        printf("nodo %u: matrices verificacion: %s (%llu discrepancias)\n",
               rank, (bad == 0) ? "OK" : "FALLO",
               (unsigned long long)bad);
        if (bad != 0) {
            return 2;
        }
    }
    return 0;
}
