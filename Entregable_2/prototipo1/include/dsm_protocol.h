#ifndef DSM_PROTOCOL_H
#define DSM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

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

void dsm_header_encode(const dsm_msg_header_t *h, uint8_t out[64]);
void dsm_header_decode(const uint8_t in[64], dsm_msg_header_t *h);

/* Serializar dsm_msg_t a header+payload bytes. Caller frees? Returns allocated buffer or NULL. */
int dsm_protocol_encode(const dsm_msg_t *msg, uint8_t **out_buf, size_t *out_len);
int dsm_protocol_decode(const uint8_t *buf, size_t len, dsm_msg_t *msg);

#endif /* DSM_PROTOCOL_H */