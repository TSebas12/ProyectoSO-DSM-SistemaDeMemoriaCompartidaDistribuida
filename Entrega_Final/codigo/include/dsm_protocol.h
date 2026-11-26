/*
 * dsm_protocol.h — Cabecera de 64 B, serialización y cargas estructuradas.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * La cabecera no cambia de tamaño respecto del E1/E2: 64 bytes exactos. En el
 * prototipo 2 se activa el campo checksum (FNV-1a de los datos) y se añaden
 * cargas estructuradas para asignación de objetos, barreras y estadísticas.
 */
#ifndef DSM_PROTOCOL_H
#define DSM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_stats.h"
#include "dsm_types.h"

typedef struct dsm_msg_header {
    uint64_t request_id;
    uint64_t page_id;
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t flags;
    uint32_t header_len;
    uint32_t payload_len;
    uint32_t src_node;
    uint32_t dst_node;
    uint32_t status;
    uint32_t checksum;
    uint32_t payload_off;
    uint16_t ttl;
    uint16_t reserved;
    uint32_t reserved2;
} dsm_msg_header_t;

_Static_assert(sizeof(dsm_msg_header_t) == 64, "dsm_msg_header_t debe medir 64 bytes");

/* --- Cargas estructuradas (van en msg->data) ---------------------------- */
/* Se declaran con tamaño fijo y sin huecos para poder enviarlas tal cual.  */

/*
 * La secuencia (seq) es la clave del contrato SPMD: es el número de llamada a
 * dsm_malloc dentro de este nodo. Todos los nodos hacen las mismas llamadas en
 * el mismo orden, así que el home puede devolver el MISMO rango global a todos
 * los que pidan la misma secuencia. Sin esto cada nodo recibiría un rango
 * distinto y no compartirían nada.
 */
typedef struct dsm_alloc_req {
    uint64_t size;      /* Bytes solicitados                            */
    uint64_t object_id; /* 0 = el home asigna uno nuevo                 */
    uint32_t seq;       /* Número de llamada dsm_malloc en el nodo      */
    uint32_t flags;
    uint32_t tag;       /* Identificador legible para dsmctl            */
    uint32_t reserved;
} dsm_alloc_req_t;

typedef struct dsm_alloc_reply {
    uint64_t object_id;
    uint64_t page_base;
    uint32_t page_count;
    uint32_t status;
} dsm_alloc_reply_t;

typedef struct dsm_barrier_req {
    uint32_t round;     /* Ronda que el nodo cree estar ejecutando      */
    uint32_t n_nodes;
} dsm_barrier_req_t;

typedef struct dsm_barrier_reply {
    uint32_t round;     /* Ronda publicada por el home                  */
    uint32_t arrived;   /* Nodos que ya llegaron a la ronda             */
    uint32_t n_nodes;
    uint32_t status;
} dsm_barrier_reply_t;

typedef struct dsm_stats_wire {
    uint32_t node_id;
    uint32_t field_count;
    uint64_t field[DSM_STATS_FIELD_COUNT];
} dsm_stats_wire_t;

typedef struct dsm_page_info {
    uint64_t page_id;
    uint64_t object_id;
    uint64_t version;
    uint32_t frame_slot;
    uint32_t home_node;
    uint32_t owner_node;
    uint32_t msi_state;
    uint32_t residency;
    uint32_t is_dirty;
    uint64_t sharers;
} dsm_page_info_t;

/* --- Checksum ----------------------------------------------------------- */
uint32_t dsm_checksum_fnv1a(const void *data, size_t len);

/* --- Cabecera ----------------------------------------------------------- */
void dsm_header_encode(const dsm_msg_header_t *h, uint8_t out[64]);
void dsm_header_decode(const uint8_t in[64], dsm_msg_header_t *h);

/* Serializa dsm_msg_t a cabecera + datos. El llamador libera *out_buf.    */
int dsm_protocol_encode(const dsm_msg_t *msg, uint8_t **out_buf, size_t *out_len);
/* Decodifica; si los datos no cuadran con el checksum devuelve -2.        */
int dsm_protocol_decode(const uint8_t *buf, size_t len, dsm_msg_t *msg);

#endif /* DSM_PROTOCOL_H */
