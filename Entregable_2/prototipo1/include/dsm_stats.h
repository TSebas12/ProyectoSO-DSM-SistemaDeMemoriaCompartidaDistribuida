/*
 * dsm_stats.h — Contadores del nodo.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * Los contadores son atómicos porque los incrementan hilos distintos (usuarios,
 * resolutor, escucha) sin tomar ningún lock. Se pueden leer en cualquier
 * momento sin detener el nodo.
 */
#ifndef DSM_STATS_H
#define DSM_STATS_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Orden de los campos: debe coincidir con el del E1 (§11.8).              */
typedef enum dsm_stats_field {
    DSM_STAT_PAGE_FAULTS          = 0,
    DSM_STAT_LOCAL_HITS           = 1,
    DSM_STAT_REMOTE_READS         = 2,
    DSM_STAT_REMOTE_WRITES        = 3,
    DSM_STAT_INVALIDATIONS_SENT   = 4,
    DSM_STAT_INVALIDATIONS_RECEIVED = 5,
    DSM_STAT_EVICTIONS            = 6,
    DSM_STAT_BYTES_SENT           = 7,
    DSM_STAT_BYTES_RECEIVED       = 8,
    DSM_STAT_MESSAGES_SENT        = 9,
    DSM_STAT_MESSAGES_RECEIVED    = 10,
    DSM_STAT_TIMEOUTS             = 11,
    DSM_STAT_ERRORS               = 12,
    DSM_STAT_UPTIME_MS            = 13,
    DSM_STAT_PAGES_RESIDENT       = 14,
    DSM_STAT_FRAMES_FREE          = 15,
    DSM_STATS_FIELD_COUNT         = 16
} dsm_stats_field_t;

/*
 * La unión anónima permite dos vistas del mismo bloque de 128 bytes: por
 * nombre (legible en el código) y por índice (para no repetir un switch de 16
 * casos en cada función).
 */
typedef struct dsm_stats {
    union {
        struct {
            _Atomic uint64_t page_faults;
            _Atomic uint64_t local_hits;
            _Atomic uint64_t remote_reads;
            _Atomic uint64_t remote_writes;
            _Atomic uint64_t invalidations_sent;
            _Atomic uint64_t invalidations_received;
            _Atomic uint64_t evictions;
            _Atomic uint64_t bytes_sent;
            _Atomic uint64_t bytes_received;
            _Atomic uint64_t messages_sent;
            _Atomic uint64_t messages_received;
            _Atomic uint64_t timeouts;
            _Atomic uint64_t errors;
            _Atomic uint64_t uptime_ms;
            _Atomic uint64_t pages_resident;
            _Atomic uint64_t frames_free;
        };
        _Atomic uint64_t field[DSM_STATS_FIELD_COUNT];
    };
} dsm_stats_t;

/* Copia no atómica, para imprimir o enviar por la red. */
typedef struct dsm_stats_view {
    uint64_t field[DSM_STATS_FIELD_COUNT];
} dsm_stats_view_t;

void        dsm_stats_init(dsm_stats_t *s);
void        dsm_stats_reset(dsm_stats_t *s);
void        dsm_stats_inc(dsm_stats_t *s, dsm_stats_field_t f);
void        dsm_stats_add(dsm_stats_t *s, dsm_stats_field_t f, uint64_t value);
void        dsm_stats_set(dsm_stats_t *s, dsm_stats_field_t f, uint64_t value);
uint64_t    dsm_stats_get(dsm_stats_t *s, dsm_stats_field_t f);
void        dsm_stats_snapshot(dsm_stats_t *s, dsm_stats_view_t *out);
const char *dsm_stats_field_name(dsm_stats_field_t f);

/* Escribe un resumen legible en buf. Devuelve los bytes escritos o -1.     */
int dsm_stats_format(dsm_stats_t *s, char *buf, size_t len);

#endif /* DSM_STATS_H */
