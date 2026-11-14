/*
 * protocol.c — Serialización de la cabecera de 64 B y verificación de datos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Novedad del prototipo 2: el campo checksum de la cabecera deja de ser
 * decorativo. Se calcula FNV-1a de 32 bits sobre los datos de página y se
 * verifica al decodificar. Un paquete corrupto se descarta antes de tocar la
 * memoria del nodo, en lugar de publicar datos inválidos.
 */
#include "dsm_protocol.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

static uint64_t htonll(uint64_t x)
{
    return ((uint64_t)htonl((uint32_t)(x >> 32))) |
           ((uint64_t)htonl((uint32_t)x) << 32);
}

static uint64_t ntohll(uint64_t x)
{
    return htonll(x);
}

uint32_t dsm_checksum_fnv1a(const void *data, size_t len)
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

void dsm_header_encode(const dsm_msg_header_t *h, uint8_t out[64])
{
    uint64_t nr = htonll(h->request_id);
    uint64_t np = htonll(h->page_id);
    uint32_t m  = htonl(h->magic);
    uint16_t v  = htons(h->version);
    uint16_t t  = htons(h->type);
    uint32_t f  = htonl(h->flags);
    uint32_t hl = htonl(h->header_len);
    uint32_t pl = htonl(h->payload_len);
    uint32_t s  = htonl(h->src_node);
    uint32_t d  = htonl(h->dst_node);
    uint32_t st = htonl(h->status);
    uint32_t c  = htonl(h->checksum);
    uint32_t po = htonl(h->payload_off);
    uint16_t ttl = htons(h->ttl);
    uint16_t res = htons(h->reserved);
    uint32_t r2 = htonl(h->reserved2);

    memcpy(out + 0,  &nr, 8);
    memcpy(out + 8,  &np, 8);
    memcpy(out + 16, &m, 4);
    memcpy(out + 20, &v, 2);
    memcpy(out + 22, &t, 2);
    memcpy(out + 24, &f, 4);
    memcpy(out + 28, &hl, 4);
    memcpy(out + 32, &pl, 4);
    memcpy(out + 36, &s, 4);
    memcpy(out + 40, &d, 4);
    memcpy(out + 44, &st, 4);
    memcpy(out + 48, &c, 4);
    memcpy(out + 52, &po, 4);
    memcpy(out + 56, &ttl, 2);
    memcpy(out + 58, &res, 2);
    memcpy(out + 60, &r2, 4);
}

void dsm_header_decode(const uint8_t in[64], dsm_msg_header_t *h)
{
    uint64_t r, p;
    uint32_t m, f, hl, pl, s, d, st, c, po, r2;
    uint16_t v, t, ttl, res;

    memcpy(&r, in + 0, 8);  h->request_id = ntohll(r);
    memcpy(&p, in + 8, 8);  h->page_id = ntohll(p);
    memcpy(&m, in + 16, 4); h->magic = ntohl(m);
    memcpy(&v, in + 20, 2); h->version = ntohs(v);
    memcpy(&t, in + 22, 2); h->type = ntohs(t);
    memcpy(&f, in + 24, 4); h->flags = ntohl(f);
    memcpy(&hl, in + 28, 4); h->header_len = ntohl(hl);
    memcpy(&pl, in + 32, 4); h->payload_len = ntohl(pl);
    memcpy(&s, in + 36, 4); h->src_node = ntohl(s);
    memcpy(&d, in + 40, 4); h->dst_node = ntohl(d);
    memcpy(&st, in + 44, 4); h->status = ntohl(st);
    memcpy(&c, in + 48, 4); h->checksum = ntohl(c);
    memcpy(&po, in + 52, 4); h->payload_off = ntohl(po);
    memcpy(&ttl, in + 56, 2); h->ttl = ntohs(ttl);
    memcpy(&res, in + 58, 2); h->reserved = ntohs(res);
    memcpy(&r2, in + 60, 4); h->reserved2 = ntohl(r2);
}

int dsm_protocol_encode(const dsm_msg_t *msg, uint8_t **out_buf, size_t *out_len)
{
    size_t          total;
    uint8_t        *buf;
    dsm_msg_header_t h;

    if (msg == NULL || out_buf == NULL || out_len == NULL) {
        return -1;
    }
    if (msg->data_len > 0 && msg->data == NULL) {
        return -1;
    }
    total = 64u + (size_t)msg->data_len;
    buf = malloc(total);
    if (buf == NULL) {
        return -1;
    }

    memset(&h, 0, sizeof(h));
    h.request_id  = msg->request_id;
    h.page_id     = msg->page_id;
    h.magic       = DSM_MAGIC;
    h.version     = DSM_PROTOCOL_VERSION;
    h.type        = msg->type;
    h.flags       = msg->flags;
    h.header_len  = 64;
    h.payload_len = msg->data_len;
    h.src_node    = msg->src_node;
    h.dst_node    = msg->dst_node;
    h.status      = (uint32_t)msg->status;
    h.checksum    = (msg->data_len > 0)
                    ? dsm_checksum_fnv1a(msg->data, msg->data_len) : 0u;
    h.payload_off = 64;
    h.ttl         = 0;

    dsm_header_encode(&h, buf);
    if (msg->data_len > 0) {
        memcpy(buf + 64, msg->data, msg->data_len);
    }
    *out_buf = buf;
    *out_len = total;
    return 0;
}

int dsm_protocol_decode(const uint8_t *buf, size_t len, dsm_msg_t *msg)
{
    dsm_msg_header_t h;
    uint32_t         expected;

    if (buf == NULL || msg == NULL || len < 64) {
        return -1;
    }
    dsm_header_decode(buf, &h);
    if (h.magic != DSM_MAGIC || h.version != DSM_PROTOCOL_VERSION) {
        return -1;
    }
    if (h.payload_len > 0 && len < 64u + (size_t)h.payload_len) {
        return -1;
    }

    memset(msg, 0, sizeof(*msg));
    msg->request_id  = h.request_id;
    msg->page_id     = h.page_id;
    msg->type        = h.type;
    msg->flags       = h.flags;
    msg->src_node    = h.src_node;
    msg->dst_node    = h.dst_node;
    msg->status      = (int32_t)h.status;
    msg->payload_len = 0;
    msg->payload     = NULL;
    msg->data_len    = h.payload_len;

    if (h.payload_len > 0) {
        expected = dsm_checksum_fnv1a(buf + 64, h.payload_len);
        if (h.checksum != 0 && expected != h.checksum) {
            return -2;
        }
        msg->data = malloc(h.payload_len);
        if (msg->data == NULL) {
            return -1;
        }
        memcpy((void *)msg->data, buf + 64, h.payload_len);
    } else {
        msg->data = NULL;
    }
    return 0;
}
