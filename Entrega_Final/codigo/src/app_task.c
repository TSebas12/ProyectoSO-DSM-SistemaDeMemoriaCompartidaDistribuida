/*
 * app_task.c — Ejecución distribuida de una tarea (Extra 1).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: Entrega final — Extra 1
 *
 * Un nodo coordinador (nodo 0) prepara una tarea en el espacio compartido:
 * un arreglo de N valores. Cada nodo (incluido el coordinador) ejecuta en
 * paralelo su porción del trabajo y publica su resultado parcial en una
 * ranura propia. El coordinador recolecta los parciales, calcula el total y
 * lo verifica contra una referencia secuencial. Se reportan speedup, balance
 * de carga y overhead de coordinación.
 *
 * Modelo: coordinador + trabajadores, sincronización por barreras, resultados
 * en DSM (una página por nodo, sin falsos compartidos).
 */
#include "dsm_apps.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_TASK_DEFAULT_N 1000000u

typedef struct task_result {
    uint64_t node_id;
    double   partial;
    uint64_t items;
    uint64_t time_us;
} task_result_t;

int dsm_app_task_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t nodes = dsm_node_count(rt);
    uint32_t rank  = dsm_node_id(rt);
    uint32_t page_size = dsm_page_size(rt);
    uint64_t n = (opts != NULL && opts->size != 0) ? (uint64_t)opts->size
                                                    : APP_TASK_DEFAULT_N;
    double  *input;
    uint8_t *results;   /* una ranura por nodo, en páginas separadas */
    uint64_t start, count;
    uint64_t i;
    uint64_t t0 = 0, t1 = 0, tseq0 = 0, tseq1 = 0;
    double   sequential = 0.0;
    double   parallel   = 0.0;
    uint64_t max_time = 0, min_time = ~0ULL;
    int      ok = 1;

    if (rt == NULL || opts == NULL) {
        return 1;
    }
    if (nodes < 2u) {
        printf("nodo %u: task: se necesitan al menos 2 nodos (hay %u)\n",
               rank, nodes);
        return -1;
    }
    if (page_size < sizeof(task_result_t)) {
        fprintf(stderr, "nodo %u: task: page_size insuficiente\n", rank);
        return 1;
    }

    /* --- Reserva: input (compartido) + resultados (una página por nodo) --- */
    input   = dsm_malloc(rt, (size_t)n * sizeof(double));
    results = dsm_malloc(rt, (size_t)nodes * (size_t)page_size);
    if (input == NULL || results == NULL) {
        fprintf(stderr, "nodo %u: task: no se pudo reservar memoria\n", rank);
        return 1;
    }
    if (rank == 0) {
        memset(results, 0, (size_t)nodes * (size_t)page_size);
    }

    /* --- Coordinador prepara la tarea --- */
    if (rank == 0) {
        for (i = 0; i < n; i++) {
            input[i] = (double)((i * 31u + 7u) % 1000u) / 100.0;
        }
        printf("nodo 0 (coordinador): tarea preparada, N=%llu elementos\n",
               (unsigned long long)n);
    }
    if (dsm_barrier(rt, rt->cfg.barrier_timeout_ms) != DSM_OK) {
        fprintf(stderr, "nodo %u: task: fallo la barrera 'preparacion' (timeout %u ms)\n", rank, rt->cfg.barrier_timeout_ms);
        return 1;
    }

    /* --- Referencia secuencial (solo el coordinador, para verificar) --- */
    if (rank == 0) {
        tseq0 = dsm_app_now_us();
        for (i = 0; i < n; i++) {
            sequential += input[i] * input[i];
        }
        tseq1 = dsm_app_now_us();
    }

    /* --- Ejecución distribuida: cada nodo procesa su porción --- */
    start = dsm_app_split_start(n, nodes, rank);
    count = dsm_app_split_count(n, nodes, rank);

    t0 = dsm_app_now_us();
    {
        double partial = 0.0;
        for (i = start; i < start + count; i++) {
            partial += input[i] * input[i];
        }
        t1 = dsm_app_now_us();
        /* Publicar resultado en la ranura propia (una página por nodo). */
        {
            task_result_t *slot = (task_result_t *)(results +
                                     (size_t)rank * page_size);

            slot->node_id  = rank;
            slot->partial  = partial;
            slot->items    = count;
            slot->time_us  = t1 - t0;
        }
    }
    if (dsm_barrier(rt, rt->cfg.barrier_timeout_ms) != DSM_OK) {
        fprintf(stderr, "nodo %u: task: fallo la barrera 'computo' (timeout %u ms)\n", rank, rt->cfg.barrier_timeout_ms);
        return 1;
    }

    /* --- Coordinador recolecta y verifica --- */
    if (rank == 0) {
        for (i = 0; i < nodes; i++) {
            task_result_t *slot = (task_result_t *)(results +
                                     (size_t)i * page_size);
            parallel += slot->partial;
            if (slot->time_us > max_time) max_time = slot->time_us;
            if (slot->time_us < min_time) min_time = slot->time_us;
            printf("nodo %u (trabajador): %llu items, parcial=%.6f, tiempo=%llu us\n",
                   (unsigned)slot->node_id,
                   (unsigned long long)slot->items,
                   slot->partial,
                   (unsigned long long)slot->time_us);
        }

        /* Verificar total. */
        if (fabs(parallel - sequential) > 1e-6) {
            ok = 0;
        }

        /* Speedup y balance. */
        {
            double speedup = (max_time > 0)
                             ? (double)(tseq1 - tseq0) / (double)max_time
                             : 0.0;
            double balance = (min_time > 0)
                             ? (double)max_time / (double)min_time
                             : 0.0;

            printf("\nnodo 0 (coordinador): total paralelo=%.6f, total secuencial=%.6f, %s\n",
                   parallel, sequential, ok ? "OK" : "FALLO");
            printf("  speedup=%.2fx, balance=%.2f, overhead_seq=%llu us\n",
                   speedup, balance, (unsigned long long)(tseq1 - tseq0));
        }

        if (!ok) {
            return 2;
        }
    } else {
        printf("nodo %u: task: porcion %llu..%llu completada (%llu us)\n",
               rank, (unsigned long long)start,
               (unsigned long long)(start + count), (unsigned long long)(t1 - t0));
    }

    if (dsm_barrier(rt, rt->cfg.barrier_timeout_ms) != DSM_OK) {
        fprintf(stderr, "nodo %u: task: fallo la barrera final (timeout %u ms)\n", rank, rt->cfg.barrier_timeout_ms);
    }
    return 0;
}