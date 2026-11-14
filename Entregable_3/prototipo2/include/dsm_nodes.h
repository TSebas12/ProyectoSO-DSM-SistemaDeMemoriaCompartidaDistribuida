/*
 * dsm_nodes.h — Tabla de nodos del clúster (quién es quién y en qué puerto).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * En el prototipo 1 la dirección de cada nodo se deducía del puerto base y el
 * destino remoto se forzaba a 127.0.0.1, lo que impedía correr más de un nodo
 * por máquina. El prototipo 2 lee un archivo de nodos con la IP real de cada
 * uno; así el mismo binario sirve para 2 o 4 máquinas y para pruebas locales.
 *
 * Formato del archivo (una línea por nodo, '#' inicia comentario):
 *
 *   0 127.0.0.1 27300
 *   1 198.51.100.32 27304
 *
 * El puerto es opcional: si se omite se usa base_port + 4 * node_id, que es la
 * regla de la pareja #3 (rango 27300-27399).
 */
#ifndef DSM_NODES_H
#define DSM_NODES_H

#include <stdint.h>
#include <stdio.h>

#include "dsm_types.h"

typedef struct dsm_peer {
    uint32_t node_id;
    char     host[64];
    uint16_t port;
    uint8_t  valid;
    uint8_t  reserved[3];
} dsm_peer_t;

typedef struct dsm_nodes {
    dsm_peer_t peer[DSM_MAX_PEERS];
    uint32_t   count;      /* Nodos distintos declarados                 */
    uint32_t   self;       /* Nodo local                                 */
    uint16_t   base_port;  /* Puerto del nodo 0                          */
    uint16_t   reserved16;
} dsm_nodes_t;

/*
 * Carga el archivo y valida que el nodo local esté declarado. base_port se usa
 * solo cuando la línea no trae puerto.
 */
dsm_status_t dsm_nodes_load(const char *path, uint32_t self, uint16_t base_port,
                            dsm_nodes_t *out);
/* Devuelve NULL si el nodo no está declarado o es el local.                */
const dsm_peer_t *dsm_nodes_get(const dsm_nodes_t *nodes, uint32_t node_id);
/* Imprime la tabla en formato de una línea por nodo.                      */
void dsm_nodes_print(const dsm_nodes_t *nodes, FILE *out);
/* Escribe el archivo con la tabla actual (lo usa ./lab.sh ip).             */
dsm_status_t dsm_nodes_save(const char *path, const dsm_nodes_t *nodes);

#endif /* DSM_NODES_H */
