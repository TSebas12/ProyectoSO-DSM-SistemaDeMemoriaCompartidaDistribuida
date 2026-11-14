/*
 * dsm_heartbeat.h — Detección de nodos vivos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Un hilo por nodo envía PING a cada peer cada heartbeat_interval_ms. Si un
 * peer no responde dentro de heartbeat_timeout_ms se marca como caído. El
 * estado se consulta con `dsmctl nodes` y se muestra en el panel web.
 *
 * El heartbeat no participa de la coherencia: si un nodo está caído, las
 * operaciones que lo necesiten fallan con DSM_ERR_TIMEOUT (política fail-stop,
 * ver la limitación correspondiente en el documento del entregable).
 */
#ifndef DSM_HEARTBEAT_H
#define DSM_HEARTBEAT_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_runtime.h"

typedef struct dsm_heartbeat dsm_heartbeat_t;

dsm_heartbeat_t *dsm_heartbeat_start(dsm_runtime_t *rt);
void             dsm_heartbeat_stop(dsm_heartbeat_t *hb);

/* 1 si el nodo respondió dentro del último periodo, 0 si no.             */
int      dsm_heartbeat_alive(dsm_heartbeat_t *hb, uint32_t node_id);
/* Milisegundos desde la última respuesta (UINT64_MAX si nunca respondió). */
uint64_t dsm_heartbeat_last_seen(dsm_heartbeat_t *hb, uint32_t node_id);
/* Tabla de nodos en texto para dsmctl.                                   */
int      dsm_heartbeat_format(dsm_heartbeat_t *hb, char *out, size_t len);

#endif /* DSM_HEARTBEAT_H */
