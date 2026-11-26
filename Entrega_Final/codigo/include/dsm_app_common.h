/*
 * dsm_app_common.h — Utilidades compartidas por las aplicaciones de prueba.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 */
#ifndef DSM_APP_COMMON_H
#define DSM_APP_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* Microsegundos del reloj monótono.                                      */
uint64_t dsm_app_now_us(void);
void     dsm_app_sleep_ms(uint32_t ms);

/*
 * Reparto en bloques contiguos: el nodo `index` de `parts` recibe
 * `[start, start + count)`. Si el total no es divisible, los primeros nodos
 * reciben un elemento más. Todos los nodos calculan lo mismo, así que el
 * reparto es determinista.
 */
uint64_t dsm_app_split_start(uint64_t total, uint32_t parts, uint32_t index);
uint64_t dsm_app_split_count(uint64_t total, uint32_t parts, uint32_t index);

/* Checksum FNV-1a de 32 bits (el mismo del protocolo).                   */
uint32_t dsm_app_checksum(const void *data, size_t len);

#endif /* DSM_APP_COMMON_H */
