/*
 * dsm_net.h — Transporte TCP con conexiones persistentes y pool de hilos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Cambios respecto del prototipo 1:
 *   - Una conexión TCP persistente por par de nodos. El cliente la reutiliza y
 *     el servidor la atiende en un hilo del pool hasta que el peer la cierre.
 *     Esto es lo que permite que el nodo "quede abierto" y siga recibiendo
 *     peticiones sin abrir un socket por mensaje.
 *   - Reintentos con backoff y contador de timeouts.
 *   - Tabla de nodos real (dsm_nodes_t) en lugar de 127.0.0.1 fijo.
 */
#ifndef DSM_NET_H
#define DSM_NET_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "dsm_config.h"
#include "dsm_coherence.h"
#include "dsm_nodes.h"
#include "dsm_stats.h"
#include "dsm_types.h"

typedef struct dsm_net dsm_net_t;

/* Lee exactamente n bytes del descriptor. 0 si los leyó, -1 si no.        */
int dsm_net_read_full(int fd, void *buf, size_t n);

dsm_net_t *dsm_net_create(const dsm_config_t *cfg, const dsm_nodes_t *nodes,
                          dsm_coherence_t *co);

/* Engancha los contadores del nodo. Sin esto las filas de red quedan en 0. */
void dsm_net_set_stats(dsm_net_t *net, dsm_stats_t *stats);

void        dsm_net_destroy(dsm_net_t *net);
dsm_status_t dsm_net_start(dsm_net_t *net);
void        dsm_net_stop(dsm_net_t *net);

/* callbacks matching dsm_coherence_ops_t */
dsm_status_t dsm_net_send(void *ctx, const dsm_msg_t *msg);
dsm_status_t dsm_net_call(void *ctx, const dsm_msg_t *req, dsm_msg_t *resp,
                          uint32_t timeout_ms);

dsm_coherence_ops_t dsm_net_ops(dsm_net_t *net);

/* --- Utilidades para heartbeat y dsmctl -------------------------------- */
/* RPC corto de vida (PING/PONG). Devuelve DSM_OK si el peer respondió.   */
dsm_status_t dsm_net_ping(dsm_net_t *net, uint32_t node_id, uint32_t timeout_ms);
/* Pide al peer su volcado de estadísticas. */
dsm_status_t dsm_net_stats(dsm_net_t *net, uint32_t node_id,
                           dsm_stats_wire_t *out, uint32_t timeout_ms);
/* Dirección y puerto configurados para un nodo (para logs y dsmctl).     */
const dsm_peer_t *dsm_net_peer(const dsm_net_t *net, uint32_t node_id);
uint32_t dsm_net_node_count(const dsm_net_t *net);
/* Cierra las conexiones salientes (se llama al apagar el nodo).          */
void dsm_net_close_peers(dsm_net_t *net);

#endif /* DSM_NET_H */
