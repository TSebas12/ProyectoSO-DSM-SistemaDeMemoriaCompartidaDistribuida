#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dsm_net.h"
#include "dsm_protocol.h"

struct dsm_net {
    dsm_config_t cfg;
    dsm_coherence_t *co;
    int listen_fd;
    pthread_t listener_thread;
    int running;
};

int dsm_net_read_full(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, (char*)buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int connect_to_node(const dsm_config_t *cfg, uint32_t node, int *fd) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    /* use localhost mapping by node port */
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int port = cfg->port_range_start + (int)node * 4;
    if (node == 0 && cfg->seed_host[0]) {
        inet_pton(AF_INET, cfg->seed_host, &addr.sin_addr);
        port = cfg->seed_port;
    }
    addr.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(s); return -1; }
    *fd = s;
    return 0;
}

static dsm_status_t send_msg_to(dsm_net_t *net, uint32_t dst, const dsm_msg_t *msg) {
    int fd;
    if (connect_to_node(&net->cfg, dst, &fd) != 0) return DSM_ERR_NET;
    uint8_t *buf; size_t len;
    if (dsm_protocol_encode(msg, &buf, &len) != 0) { close(fd); return DSM_ERR_NOMEM; }
    size_t sent = 0;
    while (sent < len) {
        ssize_t r = send(fd, buf + sent, len - sent, 0);
        if (r <= 0) { free(buf); close(fd); return DSM_ERR_NET; }
        sent += (size_t)r;
    }
    free(buf);
    close(fd);
    return DSM_OK;
}

dsm_status_t dsm_net_send(void *ctx, const dsm_msg_t *msg) {
    dsm_net_t *net = (dsm_net_t*)ctx;
    if (!net || !msg) return DSM_ERR_INVAL;
    return send_msg_to(net, msg->dst_node, msg);
}

static dsm_status_t rpc(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req, dsm_msg_t *resp) {
    int fd;
    if (connect_to_node(&net->cfg, dst, &fd) != 0) return DSM_ERR_NET;
    uint8_t *buf; size_t len;
    if (dsm_protocol_encode(req, &buf, &len) != 0) { close(fd); return DSM_ERR_NOMEM; }
    size_t sent=0;
    while(sent<len){ssize_t r=send(fd,buf+sent,len-sent,0);if(r<=0){free(buf);close(fd);return DSM_ERR_NET;}sent+=(size_t)r;}
    free(buf);
    /* read response */
    uint8_t hdr[64];
    if (dsm_net_read_full(fd, hdr, 64) != 0) { close(fd); return DSM_ERR_NET; }
    dsm_msg_header_t h; dsm_header_decode(hdr,&h);
    size_t total = 64 + h.payload_len;
    uint8_t *rbuf = malloc(total);
    if (!rbuf) { close(fd); return DSM_ERR_NOMEM; }
    memcpy(rbuf, hdr, 64);
    if (h.payload_len) {
        if (dsm_net_read_full(fd, rbuf+64, h.payload_len) != 0) { free(rbuf); close(fd); return DSM_ERR_NET; }
    }
    if (dsm_protocol_decode(rbuf, total, resp) != 0) { free(rbuf); close(fd); return DSM_ERR_PROTOCOL; }
    free(rbuf);
    close(fd);
    return DSM_OK;
}

dsm_status_t dsm_net_fetch_shared(void *ctx, uint64_t page_id, void *buf, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;
    dsm_net_t *net = (dsm_net_t*)ctx;
    dsm_msg_t req; memset(&req,0,sizeof(req));
    req.type = DSM_MSG_PAGE_REQUEST;
    req.src_node = net->cfg.node_id;
    req.dst_node = 0; /* home node 0 */
    req.request_id = page_id;
    req.page_id = page_id;
    dsm_msg_t resp; memset(&resp,0,sizeof(resp));
    dsm_status_t st = rpc(net, 0, &req, &resp);
    if (st != DSM_OK) return st;
    if (resp.type != DSM_MSG_PAGE_DATA) return DSM_ERR_PROTOCOL;
    if (resp.data && resp.data_len >= len) memcpy(buf, resp.data, len);
    if (resp.data) free((void*)resp.data);
    return (dsm_status_t)resp.status;
}

dsm_status_t dsm_net_fetch_exclusive(void *ctx, uint64_t page_id, void *buf, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;
    dsm_net_t *net = (dsm_net_t*)ctx;
    dsm_msg_t req; memset(&req,0,sizeof(req));
    req.type = DSM_MSG_OWNERSHIP_REQUEST;
    req.src_node = net->cfg.node_id;
    req.dst_node = 0;
    req.request_id = page_id;
    req.page_id = page_id;
    dsm_msg_t resp; memset(&resp,0,sizeof(resp));
    dsm_status_t st = rpc(net, 0, &req, &resp);
    if (st != DSM_OK) return st;
    if (resp.type != DSM_MSG_OWNERSHIP_GRANT) return DSM_ERR_PROTOCOL;
    if (resp.data && resp.data_len >= len) memcpy(buf, resp.data, len);
    if (resp.data) free((void*)resp.data);
    return (dsm_status_t)resp.status;
}

dsm_status_t dsm_net_writeback(void *ctx, uint64_t page_id, const void *buf, size_t len) {
    dsm_net_t *net = (dsm_net_t*)ctx;
    dsm_msg_t req; memset(&req,0,sizeof(req));
    req.type = DSM_MSG_PAGE_WRITEBACK;
    req.src_node = net->cfg.node_id;
    req.dst_node = 0;
    req.request_id = page_id;
    req.page_id = page_id;
    req.data = (const uint8_t*)buf;
    req.data_len = (uint32_t)len;
    req.flags = DSM_FLAG_HAS_DATA;
    dsm_msg_t resp; memset(&resp,0,sizeof(resp));
    dsm_status_t st = rpc(net, 0, &req, &resp);
    if (resp.data) free((void*)resp.data);
    return st;
}

/* listener */
static void *listener_loop(void *arg) {
    dsm_net_t *net = arg;
    while (net->running) {
        int cfd = accept(net->listen_fd, NULL, NULL);
        if (cfd < 0) continue;
        uint8_t hdr[64];
        if (dsm_net_read_full(cfd, hdr, 64) != 0) { close(cfd); continue; }
        dsm_msg_header_t h; dsm_header_decode(hdr,&h);
        size_t total = 64 + h.payload_len;
        uint8_t *buf = malloc(total);
        if (!buf) { close(cfd); continue; }
        memcpy(buf, hdr, 64);
        if (h.payload_len) {
            if (dsm_net_read_full(cfd, buf+64, h.payload_len) != 0) { free(buf); close(cfd); continue; }
        }
        dsm_msg_t msg; memset(&msg,0,sizeof(msg));
        if (dsm_protocol_decode(buf, total, &msg) == 0) {
            /* handle only server-side types */
            dsm_coherence_handle_message(net->co, &msg);
        }
        if (msg.data) free((void*)msg.data);
        free(buf);
        close(cfd);
    }
    return NULL;
}

dsm_net_t *dsm_net_create(const dsm_config_t *cfg, dsm_coherence_t *co) {
    dsm_net_t *net = calloc(1, sizeof(*net));
    if (!net) return NULL;
    net->cfg = *cfg;
    net->co = co;
    net->listen_fd = -1;
    return net;
}

dsm_status_t dsm_net_start(dsm_net_t *net) {
    if (!net) return DSM_ERR_INVAL;
    net->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (net->listen_fd < 0) return DSM_ERR_NET;
    int opt = 1; setsockopt(net->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, net->cfg.bind_address[0]?net->cfg.bind_address:"0.0.0.0", &addr.sin_addr);
    addr.sin_port = htons(net->cfg.listen_port);
    if (bind(net->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) return DSM_ERR_NET;
    if (listen(net->listen_fd, 16) != 0) return DSM_ERR_NET;
    net->running = 1;
    pthread_create(&net->listener_thread, NULL, listener_loop, net);
    return DSM_OK;
}

void dsm_net_stop(dsm_net_t *net) {
    if (!net) return;
    net->running = 0;
    if (net->listen_fd >= 0) { close(net->listen_fd); net->listen_fd = -1; }
    pthread_join(net->listener_thread, NULL);
}

void dsm_net_destroy(dsm_net_t *net) {
    if (!net) return;
    dsm_net_stop(net);
    free(net);
}

dsm_coherence_ops_t dsm_net_ops(dsm_net_t *net) {
    dsm_coherence_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.ctx = net;
    ops.send = dsm_net_send;
    ops.fetch_shared = dsm_net_fetch_shared;
    ops.fetch_exclusive = dsm_net_fetch_exclusive;
    ops.writeback = dsm_net_writeback;
    return ops;
}