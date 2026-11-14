/*
 * app_counter.c — Incrementos concurrentes con falsos compartidos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Cada hilo incrementa su propia ranura (no hay escrituras concurrentes a la
 * misma ranura, así que el resultado es determinista) y además lee la ranura
 * del nodo vecino. Esa lectura es la que provoca invalidaciones: mientras el
 * vecino tiene la página en M, la lectura la baja a S y el siguiente incremento
 * del vecino tiene que recuperar la propiedad. Es el patrón clásico de falsos
 * compartidos y sirve para ver el contador de invalidaciones moverse.
 */
#include "dsm_apps.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_BARRIER_MS 60000u
#define APP_MAX_THREADS 16u

typedef struct counter_arg {
    dsm_runtime_t   *rt;
    uint64_t        *counters;
    uint32_t         slot;
    uint32_t         neighbor_slot;
    uint32_t         iters;
    _Atomic uint32_t *done;
} counter_arg_t;

static void *counter_worker(void *ptr)
{
    counter_arg_t *arg = ptr;
    volatile uint64_t sink = 0;
    uint32_t i;

    if (dsm_attach(arg->rt) != DSM_OK) {
        atomic_fetch_add_explicit(arg->done, 1u, memory_order_relaxed);
        return NULL;
    }
    for (i = 0; i < arg->iters; i++) {
        arg->counters[arg->slot]++;
        sink += arg->counters[arg->neighbor_slot];
    }
    (void)sink;
    dsm_detach(arg->rt);
    atomic_fetch_add_explicit(arg->done, 1u, memory_order_relaxed);
    return NULL;
}

int dsm_app_counter_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t      rank  = dsm_node_id(rt);
    uint32_t      nodes = dsm_node_count(rt);
    uint32_t      threads = (opts->threads == 0) ? 2u : opts->threads;
    uint32_t      iters = (opts->iters == 0) ? 100u : opts->iters;
    uint64_t     *counters;
    pthread_t     tids[APP_MAX_THREADS];
    counter_arg_t args[APP_MAX_THREADS];
    _Atomic uint32_t done = 0;
    uint32_t      t;

    if (nodes == 0) {
        nodes = 1;
    }
    if (threads > APP_MAX_THREADS) {
        threads = APP_MAX_THREADS;
    }

    counters = dsm_malloc(rt, (size_t)nodes * (size_t)threads * sizeof(uint64_t));
    if (counters == NULL) {
        fprintf(stderr, "nodo %u: counter: no se pudo reservar el arreglo\n", rank);
        return 1;
    }

    for (t = 0; t < threads; t++) {
        args[t].rt            = rt;
        args[t].counters      = counters;
        args[t].slot          = rank * threads + t;
        args[t].neighbor_slot = (((rank + 1u) % nodes) * threads) + t;
        args[t].iters         = iters;
        args[t].done          = &done;
        if (pthread_create(&tids[t], NULL, counter_worker, &args[t]) != 0) {
            fprintf(stderr, "nodo %u: counter: no se pudo crear el hilo %u\n",
                    rank, t);
            args[t].iters = 0;
            atomic_fetch_add_explicit(&done, 1u, memory_order_relaxed);
            tids[t] = pthread_self();
        }
    }
    for (t = 0; t < threads; t++) {
        if (!pthread_equal(tids[t], pthread_self())) {
            pthread_join(tids[t], NULL);
        }
    }

    (void)dsm_barrier(rt, APP_BARRIER_MS);

    if (rank == 0) {
        uint64_t total = 0;
        uint64_t expected = (uint64_t)nodes * (uint64_t)threads * (uint64_t)iters;
        uint32_t i;

        for (i = 0; i < nodes * threads; i++) {
            total += counters[i];
        }
        printf("nodo 0: counter: suma = %llu, esperado = %llu, %s\n",
               (unsigned long long)total, (unsigned long long)expected,
               (total == expected) ? "OK" : "FALLO");
        if (total != expected) {
            return 2;
        }
    } else {
        printf("nodo %u: counter: %u hilos x %u incrementos\n", rank, threads, iters);
    }
    return 0;
}
