/*
 * dsm_api.h — API que usan las aplicaciones.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * El prototipo 1 solo exponía init/create/attach porque la aplicación era una
 * demostración fija. El prototipo 2 necesita que una aplicación real pida
 * memoria compartida, sincronice y consulte:
 *
 *   dsm_malloc()   reserva páginas del espacio global y devuelve una dirección
 *                  de la ventana local. La misma llamada en todos los nodos
 *                  devuelve el mismo offset (contrato SPMD).
 *   dsm_free()     devuelve el rango al asignador. En el prototipo 2 el
 *                  asignador es monótono: free no reutiliza huecos.
 *   dsm_barrier()  espera a que todos los nodos lleguen a la ronda actual.
 *   dsm_stats()    copia los contadores del nodo.
 */
#ifndef DSM_API_H
#define DSM_API_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_config.h"
#include "dsm_stats.h"
#include "dsm_types.h"

typedef struct dsm_runtime dsm_runtime_t;

dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out);
void         dsm_shutdown(dsm_runtime_t *rt);
dsm_status_t dsm_create(dsm_runtime_t *rt);
/*
 * Registra el hilo actual en el manejador de SIGSEGV. Cada hilo de la
 * aplicación debe llamarla antes de tocar la ventana; el runtime guarda un
 * contexto distinto por hilo (en el prototipo 1 todos compartían uno).
 */
dsm_status_t dsm_attach(dsm_runtime_t *rt);
void         dsm_detach(dsm_runtime_t *rt);

/* --- Memoria compartida -------------------------------------------------- */
void        *dsm_malloc(dsm_runtime_t *rt, size_t size);
void        *dsm_calloc(dsm_runtime_t *rt, size_t count, size_t size);
void         dsm_free(dsm_runtime_t *rt, void *ptr);
/* Traduce una dirección de la ventana a page_id global. */
uint64_t     dsm_addr_to_page(dsm_runtime_t *rt, const void *addr);

/* --- Sincronización ------------------------------------------------------ */
dsm_status_t dsm_barrier(dsm_runtime_t *rt, uint32_t timeout_ms);

/* --- Consulta ------------------------------------------------------------ */
uint32_t dsm_node_id(dsm_runtime_t *rt);
uint32_t dsm_node_count(dsm_runtime_t *rt);
uint32_t dsm_page_size(dsm_runtime_t *rt);
void     dsm_stats(dsm_runtime_t *rt, dsm_stats_view_t *out);

#endif /* DSM_API_H */
