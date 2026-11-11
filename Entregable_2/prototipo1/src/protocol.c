#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_protocol.h"

static uint64_t htonll(uint64_t x){return ((uint64_t)htonl((uint32_t)(x>>32))) | ((uint64_t)htonl((uint32_t)x) << 32);}
static uint64_t ntohll(uint64_t x){return htonll(x);}

void dsm_header_encode(const dsm_msg_header_t*h,uint8_t out[64]){
    uint64_t r=h->request_id,p=h->page_id;
    uint64_t nr=htonll(r), np=htonll(p);
    memcpy(out+0,&nr,8); memcpy(out+8,&np,8);
    uint32_t m=htonl(h->magic); memcpy(out+16,&m,4);
    uint16_t v=htons(h->version); memcpy(out+20,&v,2);
    uint16_t t=htons(h->type); memcpy(out+22,&t,2);
    uint32_t f=htonl(h->flags); memcpy(out+24,&f,4);
    uint32_t hl=htonl(h->header_len); memcpy(out+28,&hl,4);
    uint32_t pl=htonl(h->payload_len); memcpy(out+32,&pl,4);
    uint32_t s=htonl(h->src_node); memcpy(out+36,&s,4);
    uint32_t d=htonl(h->dst_node); memcpy(out+40,&d,4);
    uint32_t st=htonl(h->status); memcpy(out+44,&st,4);
    uint32_t c=htonl(h->checksum); memcpy(out+48,&c,4);
    uint32_t po=htonl(h->payload_off); memcpy(out+52,&po,4);
    uint16_t ttl=htons(h->ttl); memcpy(out+56,&ttl,2);
    uint16_t res=htons(h->reserved); memcpy(out+58,&res,2);
    uint32_t r2=htonl(h->reserved2); memcpy(out+60,&r2,4);
}
void dsm_header_decode(const uint8_t in[64],dsm_msg_header_t*h){
    uint64_t r,p; memcpy(&r,in+0,8); memcpy(&p,in+8,8);
    h->request_id=ntohll(r); h->page_id=ntohll(p);
    uint32_t m; memcpy(&m,in+16,4); h->magic=ntohl(m);
    uint16_t v; memcpy(&v,in+20,2); h->version=ntohs(v);
    uint16_t t; memcpy(&t,in+22,2); h->type=ntohs(t);
    uint32_t f; memcpy(&f,in+24,4); h->flags=ntohl(f);
    uint32_t hl; memcpy(&hl,in+28,4); h->header_len=ntohl(hl);
    uint32_t pl; memcpy(&pl,in+32,4); h->payload_len=ntohl(pl);
    uint32_t s; memcpy(&s,in+36,4); h->src_node=ntohl(s);
    uint32_t d; memcpy(&d,in+40,4); h->dst_node=ntohl(d);
    uint32_t st; memcpy(&st,in+44,4); h->status=ntohl(st);
    uint32_t c; memcpy(&c,in+48,4); h->checksum=ntohl(c);
    uint32_t po; memcpy(&po,in+52,4); h->payload_off=ntohl(po);
    uint16_t ttl; memcpy(&ttl,in+56,2); h->ttl=ntohs(ttl);
    uint16_t res; memcpy(&res,in+58,2); h->reserved=ntohs(res);
    uint32_t r2; memcpy(&r2,in+60,4); h->reserved2=ntohl(r2);
}

int dsm_protocol_encode(const dsm_msg_t*msg,uint8_t**out_buf,size_t*out_len){
    if(!msg||!out_buf||!out_len) return -1;
    size_t total = 64 + msg->data_len;
    uint8_t *buf = malloc(total);
    if(!buf) return -1;
    dsm_msg_header_t h; memset(&h,0,sizeof(h));
    h.request_id = msg->request_id;
    h.page_id = msg->page_id;
    h.magic = DSM_MAGIC;
    h.version = DSM_PROTOCOL_VERSION;
    h.type = msg->type;
    h.flags = msg->flags;
    h.header_len = 64;
    h.payload_len = msg->data_len;
    h.src_node = msg->src_node;
    h.dst_node = msg->dst_node;
    h.status = (uint32_t)msg->status;
    h.checksum = 0;
    h.payload_off = 64;
    h.ttl = 0;
    dsm_header_encode(&h, buf);
    if (msg->data && msg->data_len) memcpy(buf+64, msg->data, msg->data_len);
    *out_buf = buf;
    *out_len = total;
    return 0;
}

int dsm_protocol_decode(const uint8_t*buf,size_t len,dsm_msg_t*msg){
    if(!buf||!msg||len<64) return -1;
    dsm_msg_header_t h; dsm_header_decode(buf,&h);
    memset(msg,0,sizeof(*msg));
    msg->request_id = h.request_id;
    msg->page_id = h.page_id;
    msg->type = h.type;
    msg->flags = h.flags;
    msg->src_node = h.src_node;
    msg->dst_node = h.dst_node;
    msg->status = (int32_t)h.status;
    msg->payload_len = 0;
    msg->payload = NULL;
    msg->data_len = h.payload_len;
    if (h.payload_len > 0 && len >= 64 + h.payload_len) {
        msg->data = malloc(h.payload_len);
        if (!msg->data) return -1;
        memcpy((void*)msg->data, buf+64, h.payload_len);
    } else {
        msg->data = NULL;
    }
    return 0;
}