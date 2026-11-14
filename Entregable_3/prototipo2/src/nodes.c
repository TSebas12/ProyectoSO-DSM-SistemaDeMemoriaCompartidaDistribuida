/*
 * nodes.c — Lectura y escritura del archivo de nodos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * El parser es deliberadamente pequeño: tres columnas, sin secciones. Se
 * aceptan espacios o tabuladores y se ignoran las líneas vacías.
 */
#include "dsm_nodes.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
{
    char  *p = s;
    size_t n;

    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static int parse_u32(const char *text, uint32_t *out)
{
    char         *end;
    unsigned long v;

    errno = 0;
    v = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v > 0xFFFFFFFFul) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

dsm_status_t dsm_nodes_load(const char *path, uint32_t self, uint16_t base_port,
                            dsm_nodes_t *out)
{
    FILE *f;
    char  line[512];
    int   found_self = 0;

    if (path == NULL || out == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(out, 0, sizeof(*out));
    out->self      = self;
    out->base_port = base_port;

    f = fopen(path, "r");
    if (f == NULL) {
        return DSM_ERR_NOENT;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char    *hash;
        char    *fields[3];
        int      nf = 0;
        char    *p;
        uint32_t id;
        uint32_t port;
        dsm_peer_t *peer;

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        trim(line);
        if (line[0] == '\0') {
            continue;
        }

        /* Se parte la línea en como máximo tres campos. */
        p = line;
        while (*p != '\0' && nf < 3) {
            while (isspace((unsigned char)*p)) {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            fields[nf++] = p;
            while (*p != '\0' && !isspace((unsigned char)*p)) {
                p++;
            }
            if (*p != '\0') {
                *p++ = '\0';
            }
        }
        if (nf < 2) {
            fclose(f);
            return DSM_ERR_INVAL;
        }
        if (parse_u32(fields[0], &id) != 0 || id >= DSM_MAX_NODES) {
            fclose(f);
            return DSM_ERR_INVAL;
        }
        if (out->peer[id].valid) {
            fclose(f);
            return DSM_ERR_EXIST;
        }
        if (nf >= 3) {
            if (parse_u32(fields[2], &port) != 0 || port == 0 || port > 65535u) {
                fclose(f);
                return DSM_ERR_INVAL;
            }
        } else {
            port = (uint32_t)base_port + 4u * id;
        }

        peer = &out->peer[id];
        peer->node_id = id;
        peer->port    = (uint16_t)port;
        peer->valid   = 1;
        snprintf(peer->host, sizeof(peer->host), "%s", fields[1]);
        out->count++;
        if (id == self) {
            found_self = 1;
        }
    }
    fclose(f);

    if (!found_self) {
        return DSM_ERR_NOENT;
    }
    return DSM_OK;
}

const dsm_peer_t *dsm_nodes_get(const dsm_nodes_t *nodes, uint32_t node_id)
{
    if (nodes == NULL || node_id >= DSM_MAX_PEERS) {
        return NULL;
    }
    if (!nodes->peer[node_id].valid) {
        return NULL;
    }
    return &nodes->peer[node_id];
}

void dsm_nodes_print(const dsm_nodes_t *nodes, FILE *out)
{
    uint32_t i;

    if (nodes == NULL || out == NULL) {
        return;
    }
    fprintf(out, "# nodo  host  puerto   (nodo local: %u)\n", nodes->self);
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        if (nodes->peer[i].valid) {
            fprintf(out, "%-5u %-15s %u%s\n",
                    nodes->peer[i].node_id,
                    nodes->peer[i].host,
                    (unsigned)nodes->peer[i].port,
                    (i == nodes->self) ? "   # local" : "");
        }
    }
}

dsm_status_t dsm_nodes_save(const char *path, const dsm_nodes_t *nodes)
{
    FILE    *f;
    uint32_t i;

    if (path == NULL || nodes == NULL) {
        return DSM_ERR_INVAL;
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return DSM_ERR_PERM;
    }
    fprintf(f, "# Tabla de nodos del DSM. Una linea por nodo: id host [puerto]\n");
    fprintf(f, "# Generado por ./lab.sh ip <nodo> <ip>\n");
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        if (nodes->peer[i].valid) {
            fprintf(f, "%u %s %u\n", nodes->peer[i].node_id,
                    nodes->peer[i].host, (unsigned)nodes->peer[i].port);
        }
    }
    fclose(f);
    return DSM_OK;
}
