/*
 * app_common.c — Utilidades compartidas por las aplicaciones de prueba.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 */
#include "dsm_app_common.h"

#include <time.h>

uint64_t dsm_app_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000L);
}

void dsm_app_sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

uint64_t dsm_app_split_start(uint64_t total, uint32_t parts, uint32_t index)
{
    uint64_t base;
    uint64_t extra;

    if (parts == 0 || index >= parts) {
        return 0;
    }
    base  = total / parts;
    extra = total % parts;
    return index * base + ((index < extra) ? index : extra);
}

uint64_t dsm_app_split_count(uint64_t total, uint32_t parts, uint32_t index)
{
    uint64_t base;
    uint64_t extra;

    if (parts == 0 || index >= parts) {
        return 0;
    }
    base  = total / parts;
    extra = total % parts;
    return base + ((index < extra) ? 1u : 0u);
}

uint32_t dsm_app_checksum(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t       h = 2166136261u;
    size_t         i;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}
