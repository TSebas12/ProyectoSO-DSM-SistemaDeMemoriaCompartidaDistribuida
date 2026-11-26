/*
 * dsm_web.h — Panel web mínimo de solo lectura.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Sirve dos rutas:
 *   GET /            tabla HTML con contadores, nodos y objetos
 *   GET /stats.json  los mismos datos en JSON
 *
 * No acepta métodos de escritura ni formularios: el enunciado advierte que la
 * web no debe distraer del núcleo del DSM.
 */
#ifndef DSM_WEB_H
#define DSM_WEB_H

#include <stddef.h>

#include "dsm_runtime.h"

typedef struct dsm_web dsm_web_t;

dsm_web_t *dsm_web_start(dsm_runtime_t *rt);
void       dsm_web_stop(dsm_web_t *web);

/* Genera el HTML del panel (lo usa el servidor y las pruebas). */
int dsm_web_render_html(dsm_runtime_t *rt, char *out, size_t len);
/* Genera el JSON de estadísticas. */
int dsm_web_render_json(dsm_runtime_t *rt, char *out, size_t len);

#endif /* DSM_WEB_H */
