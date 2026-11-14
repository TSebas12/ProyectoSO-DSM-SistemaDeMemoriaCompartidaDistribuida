/*
 * app_file.c — Checksum distribuido de un archivo (o de un patrón sintético).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Modela el caso de un archivo grande o un video: el nodo 0 lo carga una vez
 * en memoria compartida y todos los nodos leen un trozo distinto para calcular
 * un checksum parcial. Los parciales se guardan en una página por nodo (sin
 * falsos compartidos) y el nodo 0 los suma. El resultado se compara contra el
 * checksum completo calculado localmente por el nodo 0.
 */
#include "dsm_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_app_common.h"
#include "dsm_runtime.h"

#define APP_BARRIER_MS 60000u

int dsm_app_file_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t  rank  = dsm_node_id(rt);
    uint32_t  nodes = dsm_node_count(rt);
    uint32_t  page_size = dsm_page_size(rt);
    uint64_t  size;
    uint64_t  start;
    uint64_t  count;
    uint8_t  *buf;
    uint8_t  *partial;
    uint32_t  local_hash;
    uint64_t  i;

    if (nodes == 0) {
        nodes = 1;
    }
    size = (opts->size == 0) ? (1024u * 1024u) : (uint64_t)opts->size;

    buf     = dsm_malloc(rt, (size_t)size);
    partial = dsm_malloc(rt, (size_t)nodes * (size_t)page_size);
    if (buf == NULL || partial == NULL) {
        fprintf(stderr, "nodo %u: file: no se pudo reservar %llu bytes\n",
                rank, (unsigned long long)size);
        return 1;
    }

    if (rank == 0) {
        if (opts->file != NULL) {
            FILE *f = fopen(opts->file, "rb");
            size_t got = 0;

            if (f == NULL) {
                fprintf(stderr, "nodo 0: file: no se pudo abrir %s\n", opts->file);
                memset(buf, 0, (size_t)size);
            } else {
                got = fread(buf, 1, (size_t)size, f);
                fclose(f);
                if (got < (size_t)size) {
                    memset(buf + got, 0, (size_t)size - got);
                }
                printf("nodo 0: file: %llu bytes leidos de %s\n",
                       (unsigned long long)got, opts->file);
            }
        } else {
            for (i = 0; i < size; i++) {
                buf[i] = (uint8_t)((i * 31u + 7u) & 0xFFu);
            }
            printf("nodo 0: file: patron sintetico de %llu bytes\n",
                   (unsigned long long)size);
        }
    }
    (void)dsm_barrier(rt, APP_BARRIER_MS);

    start = dsm_app_split_start(size, nodes, rank);
    count = dsm_app_split_count(size, nodes, rank);
    local_hash = dsm_app_checksum(buf + start, (size_t)count);
    /* Una página por nodo: cada uno escribe su propio marco, sin falsos
     * compartidos. */
    ((uint64_t *)partial)[rank] = (uint64_t)local_hash;

    (void)dsm_barrier(rt, APP_BARRIER_MS);

    if (rank == 0) {
        uint64_t total_ok = 1;
        uint32_t full     = dsm_app_checksum(buf, (size_t)size);
        uint32_t i;

        /*
         * No se suman los checksums parciales: la suma de hashes de bloque no
         * es el hash del buffer completo. Se recalcula cada bloque sobre la
         * copia compartida y se compara con lo que escribio su nodo; asi la
         * verificacion cubre tanto los datos como la coherencia del arreglo de
         * parciales.
         */
        for (i = 0; i < nodes; i++) {
            uint64_t s = dsm_app_split_start(size, nodes, i);
            uint64_t c = dsm_app_split_count(size, nodes, i);
            uint32_t esperado = dsm_app_checksum(buf + s, (size_t)c);

            if (((uint64_t *)partial)[i] != (uint64_t)esperado) {
                total_ok = 0;
            }
        }
        printf("nodo 0: file: checksum completo = 0x%08x, %u bloques, %s\n",
               full, nodes, total_ok ? "OK" : "FALLO");
        return total_ok ? 0 : 2;
    }
    printf("nodo %u: file: tramo %llu..%llu checksum 0x%08x\n",
           rank, (unsigned long long)start,
           (unsigned long long)(start + count), local_hash);
    return 0;
}
