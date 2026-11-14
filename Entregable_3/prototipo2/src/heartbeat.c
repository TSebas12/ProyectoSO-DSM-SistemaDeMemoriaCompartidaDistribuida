/*
 * heartbeat.c — Detección de nodos vivos con PING/PONG.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Un solo hilo recorre los peers y les manda un PING con tiempo límite. El
 * resultado se guarda en dos arreglos: vivo/no vivo y el instante de la última
 * respuesta. La coherencia no depende de este módulo: si un nodo está caído,
 * la operación que lo necesite falla con DSM_ERR_TIMEOUT (fail-stop).
 */
#include "dsm_heartbeat.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dsm_net.h"
#include "dsm_nodes.h"
#include "dsm_sync.h"

struct dsm_heartbeat {
    dsm_runtime_t *rt;
    pthread_t      thread;
    _Atomic int    running;
    int            started;
    int            alive[DSM_MAX_PEERS];
    uint64_t       last_seen_ms[DSM_MAX_PEERS];
};

static void hb_sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static void *heartbeat_loop(void *arg)
{
    dsm_heartbeat_t *hb = arg;
    dsm_runtime_t   *rt = hb->rt;

    while (atomic_load_explicit(&hb->running, memory_order_acquire)) {
        uint32_t i;

        /*
         * Primera ronda con retraso: los nodos arrancan casi a la vez y, sin
         * esta pausa, el primer PING llega antes de que el otro haga listen().
         * Eso no es un fallo real y ensuciaría los contadores de timeout.
         */
        hb_sleep_ms(rt->cfg.heartbeat_interval_ms);
        if (!atomic_load_explicit(&hb->running, memory_order_acquire)) {
            break;
        }

        for (i = 0; i < DSM_MAX_PEERS; i++) {
            const dsm_peer_t *p;
            dsm_status_t      st;

            if (i == rt->cfg.node_id) {
                continue;
            }
            p = dsm_nodes_get(&rt->nodes, i);
            if (p == NULL) {
                continue;
            }
            st = dsm_net_ping(rt->net, i, rt->cfg.heartbeat_timeout_ms);
            if (st == DSM_OK) {
                hb->alive[i]        = 1;
                hb->last_seen_ms[i] = dsm_now_ms();
            } else {
                hb->alive[i] = 0;
            }
        }
    }
    return NULL;
}

dsm_heartbeat_t *dsm_heartbeat_start(dsm_runtime_t *rt)
{
    dsm_heartbeat_t *hb;
    uint32_t         i;

    if (rt == NULL || rt->net == NULL) {
        return NULL;
    }
    hb = calloc(1, sizeof(*hb));
    if (hb == NULL) {
        return NULL;
    }
    hb->rt = rt;
    atomic_init(&hb->running, 0);
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        hb->alive[i]        = (i == rt->cfg.node_id) ? 1 : 0;
        hb->last_seen_ms[i] = UINT64_MAX;
    }
    hb->last_seen_ms[rt->cfg.node_id] = dsm_now_ms();

    atomic_store_explicit(&hb->running, 1, memory_order_release);
    if (pthread_create(&hb->thread, NULL, heartbeat_loop, hb) != 0) {
        atomic_store_explicit(&hb->running, 0, memory_order_release);
        free(hb);
        return NULL;
    }
    hb->started = 1;
    return hb;
}

void dsm_heartbeat_stop(dsm_heartbeat_t *hb)
{
    if (hb == NULL) {
        return;
    }
    atomic_store_explicit(&hb->running, 0, memory_order_release);
    if (hb->started) {
        pthread_join(hb->thread, NULL);
        hb->started = 0;
    }
    free(hb);
}

int dsm_heartbeat_alive(dsm_heartbeat_t *hb, uint32_t node_id)
{
    if (hb == NULL || node_id >= DSM_MAX_PEERS) {
        return 0;
    }
    return hb->alive[node_id];
}

uint64_t dsm_heartbeat_last_seen(dsm_heartbeat_t *hb, uint32_t node_id)
{
    if (hb == NULL || node_id >= DSM_MAX_PEERS) {
        return UINT64_MAX;
    }
    return hb->last_seen_ms[node_id];
}

int dsm_heartbeat_format(dsm_heartbeat_t *hb, char *out, size_t len)
{
    size_t   used = 0;
    uint32_t i;
    uint64_t now;

    if (hb == NULL || out == NULL || len == 0) {
        return -1;
    }
    now = dsm_now_ms();
    used += (size_t)snprintf(out + used, len - used,
                             "%-5s %-16s %-6s %s\n",
                             "nodo", "host", "estado", "ultima respuesta");
    if (used >= len) {
        return -1;
    }
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        const dsm_peer_t *p = dsm_nodes_get(&hb->rt->nodes, i);
        int               n;

        if (p == NULL) {
            continue;
        }
        if (hb->last_seen_ms[i] == UINT64_MAX) {
            n = snprintf(out + used, len - used, "%-5u %-16s %-6s %s\n",
                         p->node_id, p->host,
                         hb->alive[i] ? "vivo" : "sin respuesta", "nunca");
        } else {
            n = snprintf(out + used, len - used, "%-5u %-16s %-6s %llu ms\n",
                         p->node_id, p->host,
                         hb->alive[i] ? "vivo" : "sin respuesta",
                         (unsigned long long)(now - hb->last_seen_ms[i]));
        }
        if (n < 0 || (size_t)n >= len - used) {
            return -1;
        }
        used += (size_t)n;
    }
    return (int)used;
}
