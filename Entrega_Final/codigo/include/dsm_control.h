/*
 * dsm_control.h — Servidor de control para dsmctl (texto sobre TCP).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Escucha en control_port. Cada conexión recibe una línea de comando y
 * devuelve texto plano; dsmctl la imprime tal cual. Comandos:
 *
 *   stats     contadores del nodo
 *   pages     páginas de la tabla local
 *   dir       directorio (solo el home tiene entradas)
 *   objects   objetos asignados
 *   nodes     nodos del clúster y su estado de heartbeat
 *   ping      responde "pong <nodo>"
 *   help      lista de comandos
 */
#ifndef DSM_CONTROL_H
#define DSM_CONTROL_H

#include <stddef.h>

#include "dsm_runtime.h"

typedef struct dsm_control dsm_control_t;

dsm_control_t *dsm_control_start(dsm_runtime_t *rt);
void           dsm_control_stop(dsm_control_t *ctl);

/* Ejecuta un comando en el nodo local sin pasar por la red (pruebas).    */
int dsm_control_dispatch(dsm_runtime_t *rt, const char *cmd, char *out, size_t len);

#endif /* DSM_CONTROL_H */
