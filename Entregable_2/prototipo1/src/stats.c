/*
 * stats.c — Implementación de los contadores atómicos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * Todas las operaciones usan memoria relajada: los contadores no ordenan nada
 * respecto de otros datos, solo cuentan. La única excepción es la instantánea,
 * que se toma campo por campo y por eso no es consistente entre sí; para
 * estadísticas eso es aceptable y se documenta en la guía.
 */
#include "dsm_stats.h"

#include <stdio.h>

_Static_assert(sizeof(dsm_stats_t) == 128, "dsm_stats_t debe medir 128 bytes");

static const char *const k_field_names[DSM_STATS_FIELD_COUNT] = {
    "page_faults",
    "local_hits",
    "remote_reads",
    "remote_writes",
    "invalidations_sent",
    "invalidations_received",
    "evictions",
    "bytes_sent",
    "bytes_received",
    "messages_sent",
    "messages_received",
    "timeouts",
    "errors",
    "uptime_ms",
    "pages_resident",
    "frames_free"
};

void dsm_stats_init(dsm_stats_t *s)
{
    int i;

    if (s == NULL) {
        return;
    }
    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        atomic_init(&s->field[i], 0u);
    }
}

void dsm_stats_reset(dsm_stats_t *s)
{
    int i;

    if (s == NULL) {
        return;
    }
    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        atomic_store_explicit(&s->field[i], 0u, memory_order_relaxed);
    }
}

void dsm_stats_inc(dsm_stats_t *s, dsm_stats_field_t f)
{
    if (s == NULL || (unsigned)f >= (unsigned)DSM_STATS_FIELD_COUNT) {
        return;
    }
    atomic_fetch_add_explicit(&s->field[f], 1u, memory_order_relaxed);
}

void dsm_stats_add(dsm_stats_t *s, dsm_stats_field_t f, uint64_t value)
{
    if (s == NULL || (unsigned)f >= (unsigned)DSM_STATS_FIELD_COUNT) {
        return;
    }
    atomic_fetch_add_explicit(&s->field[f], value, memory_order_relaxed);
}

void dsm_stats_set(dsm_stats_t *s, dsm_stats_field_t f, uint64_t value)
{
    if (s == NULL || (unsigned)f >= (unsigned)DSM_STATS_FIELD_COUNT) {
        return;
    }
    atomic_store_explicit(&s->field[f], value, memory_order_relaxed);
}

uint64_t dsm_stats_get(dsm_stats_t *s, dsm_stats_field_t f)
{
    if (s == NULL || (unsigned)f >= (unsigned)DSM_STATS_FIELD_COUNT) {
        return 0;
    }
    return atomic_load_explicit(&s->field[f], memory_order_relaxed);
}

void dsm_stats_snapshot(dsm_stats_t *s, dsm_stats_view_t *out)
{
    int i;

    if (s == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        out->field[i] = atomic_load_explicit(&s->field[i], memory_order_relaxed);
    }
}

const char *dsm_stats_field_name(dsm_stats_field_t f)
{
    if ((unsigned)f >= (unsigned)DSM_STATS_FIELD_COUNT) {
        return "?";
    }
    return k_field_names[f];
}

int dsm_stats_format(dsm_stats_t *s, char *buf, size_t len)
{
    dsm_stats_view_t v;
    int written;

    if (s == NULL || buf == NULL || len == 0) {
        return -1;
    }
    dsm_stats_snapshot(s, &v);

    written = snprintf(buf, len,
        "fallos de pagina      : %llu\n"
        "aciertos locales      : %llu\n"
        "lecturas remotas      : %llu\n"
        "escrituras remotas    : %llu\n"
        "invalidaciones enviadas : %llu\n"
        "invalidaciones recibidas: %llu\n"
        "expulsiones           : %llu\n"
        "bytes enviados        : %llu\n"
        "bytes recibidos       : %llu\n"
        "mensajes enviados     : %llu\n"
        "mensajes recibidos    : %llu\n"
        "tiempos agotados      : %llu\n"
        "errores               : %llu\n"
        "tiempo activo (ms)    : %llu\n"
        "paginas residentes    : %llu\n"
        "marcos libres         : %llu\n",
        (unsigned long long)v.field[DSM_STAT_PAGE_FAULTS],
        (unsigned long long)v.field[DSM_STAT_LOCAL_HITS],
        (unsigned long long)v.field[DSM_STAT_REMOTE_READS],
        (unsigned long long)v.field[DSM_STAT_REMOTE_WRITES],
        (unsigned long long)v.field[DSM_STAT_INVALIDATIONS_SENT],
        (unsigned long long)v.field[DSM_STAT_INVALIDATIONS_RECEIVED],
        (unsigned long long)v.field[DSM_STAT_EVICTIONS],
        (unsigned long long)v.field[DSM_STAT_BYTES_SENT],
        (unsigned long long)v.field[DSM_STAT_BYTES_RECEIVED],
        (unsigned long long)v.field[DSM_STAT_MESSAGES_SENT],
        (unsigned long long)v.field[DSM_STAT_MESSAGES_RECEIVED],
        (unsigned long long)v.field[DSM_STAT_TIMEOUTS],
        (unsigned long long)v.field[DSM_STAT_ERRORS],
        (unsigned long long)v.field[DSM_STAT_UPTIME_MS],
        (unsigned long long)v.field[DSM_STAT_PAGES_RESIDENT],
        (unsigned long long)v.field[DSM_STAT_FRAMES_FREE]);

    if (written < 0) {
        return -1;
    }
    return written;
}
