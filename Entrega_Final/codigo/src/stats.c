/*
 * stats.c — Implementación de los contadores atómicos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Todas las operaciones usan memoria relajada: los contadores no ordenan nada
 * respecto de otros datos, solo cuentan. La única excepción es la instantánea,
 * que se toma campo por campo y por eso no es consistente entre sí; para
 * estadísticas eso es aceptable y se documenta en la guía.
 */
#include "dsm_stats.h"

#include <stdio.h>

_Static_assert(sizeof(dsm_stats_t) == 192, "dsm_stats_t debe medir 192 bytes");

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
    "frames_free",
    "forwardings",
    "evictions_dirty",
    "swap_bytes",
    "rpc_count",
    "rpc_latency_us",
    "retries",
    "barriers",
    "releases"
};

static const char *const k_field_labels[DSM_STATS_FIELD_COUNT] = {
    "fallos de pagina",
    "aciertos locales",
    "lecturas remotas",
    "escrituras remotas",
    "invalidaciones enviadas",
    "invalidaciones recibidas",
    "expulsiones",
    "bytes enviados",
    "bytes recibidos",
    "mensajes enviados",
    "mensajes recibidos",
    "tiempos agotados",
    "errores",
    "tiempo activo (ms)",
    "paginas residentes",
    "marcos libres",
    "reenvios de terceros",
    "expulsiones sucias",
    "bytes de swap",
    "rpc totales",
    "latencia rpc total (us)",
    "reintentos",
    "barreras",
    "liberaciones"
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

uint64_t dsm_stats_avg_rpc_us(dsm_stats_t *s)
{
    uint64_t count;
    uint64_t total;

    count = dsm_stats_get(s, DSM_STAT_RPC_COUNT);
    if (count == 0) {
        return 0;
    }
    total = dsm_stats_get(s, DSM_STAT_RPC_LATENCY_US);
    return total / count;
}

int dsm_stats_format(dsm_stats_t *s, char *buf, size_t len)
{
    dsm_stats_view_t v;
    int              written = 0;
    size_t           used = 0;
    int              i;

    if (s == NULL || buf == NULL || len == 0) {
        return -1;
    }
    dsm_stats_snapshot(s, &v);

    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        int n = snprintf(buf + used, len - used, "%-25s: %llu\n",
                         k_field_labels[i],
                         (unsigned long long)v.field[i]);
        if (n < 0 || (size_t)n >= len - used) {
            return -1;
        }
        used += (size_t)n;
        written += n;
    }
    return written;
}

int dsm_stats_format_kv(dsm_stats_t *s, char *buf, size_t len)
{
    dsm_stats_view_t v;
    size_t           used = 0;
    int              i;

    if (s == NULL || buf == NULL || len == 0) {
        return -1;
    }
    dsm_stats_snapshot(s, &v);
    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        int n = snprintf(buf + used, len - used, "%s = %llu\n",
                         k_field_names[i],
                         (unsigned long long)v.field[i]);
        if (n < 0 || (size_t)n >= len - used) {
            return -1;
        }
        used += (size_t)n;
    }
    return (int)used;
}
