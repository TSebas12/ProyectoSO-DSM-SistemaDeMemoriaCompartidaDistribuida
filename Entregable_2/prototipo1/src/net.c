#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "dsm_net.h"
#include "dsm_protocol.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct dsm_net {
    dsm_config_t cfg;
    dsm_coherence_t *co;
    dsm_stats_t *stats;      /* opcional: contadores del nodo */
    int listen_fd;
    pthread_t listener_thread;
    _Atomic int running;
    int started;
};

/*
 * Socket por el que debe salir la respuesta del mensaje que se está
 * atendiendo. El hilo de escucha lo fija antes de llamar a la coherencia y lo
 * limpia al terminar. Así un PAGE_DATA viaja por la conexión del RPC en vez de
 * abrir una conexión nueva hacia el solicitante.
 */
static _Thread_local int t_reply_fd = -1;

static void set_timeouts(int fd, uint32_t ms)
{
    struct timeval tv;

    if (ms == 0) {
        return;
    }
    tv.tv_sec  = (time_t)(ms / 1000u);
    tv.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* Serializa y escribe un mensaje completo en un socket ya conectado. */
static int write_msg_fd(dsm_net_t *net, int fd, const dsm_msg_t *msg)
{
    uint8_t *buf;
    size_t   len;
    size_t   sent = 0;

    if (dsm_protocol_encode(msg, &buf, &len) != 0) {
        return -1;
    }
    while (sent < len) {
        ssize_t r = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (r <= 0) {
            free(buf);
            return -1;
        }
        sent += (size_t)r;
    }
    free(buf);
    if (net != NULL && net->stats != NULL) {
        dsm_stats_inc(net->stats, DSM_STAT_MESSAGES_SENT);
        dsm_stats_add(net->stats, DSM_STAT_BYTES_SENT, (uint64_t)len);
    }
    return 0;
}

/* Contabiliza un mensaje que llegó completo (cabecera + carga útil). */
static void note_received(dsm_net_t *net, size_t bytes)
{
    if (net != NULL && net->stats != NULL) {
        dsm_stats_inc(net->stats, DSM_STAT_MESSAGES_RECEIVED);
        dsm_stats_add(net->stats, DSM_STAT_BYTES_RECEIVED, (uint64_t)bytes);
    }
}

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
    set_timeouts(s, cfg->connect_timeout_ms);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(s); return -1; }
    set_timeouts(s, cfg->network_timeout_ms);
    *fd = s;
    return 0;
}

static dsm_status_t send_msg_to(dsm_net_t *net, uint32_t dst, const dsm_msg_t *msg) {
    int fd;
    if (connect_to_node(&net->cfg, dst, &fd) != 0) return DSM_ERR_NET;
    if (write_msg_fd(net, fd, msg) != 0) { close(fd); return DSM_ERR_NET; }
    close(fd);
    return DSM_OK;
}

dsm_status_t dsm_net_send(void *ctx, const dsm_msg_t *msg) {
    dsm_net_t *net = (dsm_net_t*)ctx;
    if (!net || !msg) return DSM_ERR_INVAL;
    /*
     * Si estamos dentro del hilo de escucha, la respuesta pertenece al RPC que
     * llegó por t_reply_fd. Fuera de él, es un envío saliente normal.
     */
    if (t_reply_fd >= 0) {
        return (write_msg_fd(net, t_reply_fd, msg) == 0) ? DSM_OK : DSM_ERR_NET;
    }
    return send_msg_to(net, msg->dst_node, msg);
}

static dsm_status_t rpc(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req, dsm_msg_t *resp) {
    int fd;
    uint8_t hdr[64];
    dsm_msg_header_t h;
    uint8_t *rbuf;
    size_t total;

    if (connect_to_node(&net->cfg, dst, &fd) != 0) return DSM_ERR_NET;
    if (write_msg_fd(net, fd, req) != 0) { close(fd); return DSM_ERR_NET; }

    /* read response */
    if (dsm_net_read_full(fd, hdr, 64) != 0) { close(fd); return DSM_ERR_NET; }
    dsm_header_decode(hdr,&h);
    if (h.magic != DSM_MAGIC || h.version != DSM_PROTOCOL_VERSION) {
        close(fd);
        return DSM_ERR_PROTOCOL;
    }
    if (h.payload_len > net->cfg.max_message_size) {
        close(fd);
        return DSM_ERR_PROTOCOL;
    }
    total = 64 + h.payload_len;
    rbuf = malloc(total);
    if (!rbuf) { close(fd); return DSM_ERR_NOMEM; }
    memcpy(rbuf, hdr, 64);
    if (h.payload_len) {
        if (dsm_net_read_full(fd, rbuf+64, h.payload_len) != 0) { free(rbuf); close(fd); return DSM_ERR_NET; }
    }
    if (dsm_protocol_decode(rbuf, total, resp) != 0) { free(rbuf); close(fd); return DSM_ERR_PROTOCOL; }
    note_received(net, total);
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
    if (resp.type != DSM_MSG_PAGE_DATA || resp.data == NULL || resp.data_len < len) {
        if (resp.data) free((void*)resp.data);
        return (resp.type == DSM_MSG_ERROR) ? (dsm_status_t)resp.status
                                            : DSM_ERR_PROTOCOL;
    }
    memcpy(buf, resp.data, len);
    free((void*)resp.data);
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
    if (resp.type != DSM_MSG_OWNERSHIP_GRANT || resp.data == NULL || resp.data_len < len) {
        if (resp.data) free((void*)resp.data);
        if (resp.type == DSM_MSG_OWNERSHIP_DENY || resp.type == DSM_MSG_ERROR) {
            return (dsm_status_t)resp.status;
        }
        return DSM_ERR_PROTOCOL;
    }
    memcpy(buf, resp.data, len);
    free((void*)resp.data);
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
    if (st == DSM_OK && resp.type != DSM_MSG_PAGE_WRITEBACK_ACK) {
        st = (resp.type == DSM_MSG_ERROR) ? (dsm_status_t)resp.status
                                          : DSM_ERR_PROTOCOL;
    }
    if (resp.data) free((void*)resp.data);
    return st;
}

/* listener */
static void *listener_loop(void *arg) {
    dsm_net_t *net = arg;
    int lfd = net->listen_fd;

    while (atomic_load_explicit(&net->running, memory_order_acquire)) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (!atomic_load_explicit(&net->running, memory_order_acquire)) break;
            continue;
        }
        set_timeouts(cfd, net->cfg.network_timeout_ms);

        uint8_t hdr[64];
        if (dsm_net_read_full(cfd, hdr, 64) != 0) { close(cfd); continue; }
        dsm_msg_header_t h; dsm_header_decode(hdr,&h);
        if (h.magic != DSM_MAGIC || h.version != DSM_PROTOCOL_VERSION ||
            h.payload_len > net->cfg.max_message_size) {
            close(cfd);
            continue;
        }
        size_t total = 64 + h.payload_len;
        uint8_t *buf = malloc(total);
        if (!buf) { close(cfd); continue; }
        memcpy(buf, hdr, 64);
        if (h.payload_len) {
            if (dsm_net_read_full(cfd, buf+64, h.payload_len) != 0) { free(buf); close(cfd); continue; }
        }
        dsm_msg_t msg; memset(&msg,0,sizeof(msg));
        if (dsm_protocol_decode(buf, total, &msg) == 0) {
            note_received(net, total);
            /*
             * La coherencia responde con ops.send(); ese envío debe volver por
             * esta misma conexión para que el rpc() del solicitante lo lea.
             */
            t_reply_fd = cfd;
            dsm_coherence_handle_message(net->co, &msg);
            t_reply_fd = -1;
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
    atomic_init(&net->running, 0);
    net->started = 0;
    return net;
}

void dsm_net_set_stats(dsm_net_t *net, dsm_stats_t *stats)
{
    if (net != NULL) {
        net->stats = stats;
    }
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
    atomic_store_explicit(&net->running, 1, memory_order_release);
    if (pthread_create(&net->listener_thread, NULL, listener_loop, net) != 0) {
        atomic_store_explicit(&net->running, 0, memory_order_release);
        close(net->listen_fd);
        net->listen_fd = -1;
        return DSM_ERR_NET;
    }
    net->started = 1;
    return DSM_OK;
}

void dsm_net_stop(dsm_net_t *net) {
    int lfd;

    if (!net) return;
    atomic_store_explicit(&net->running, 0, memory_order_release);
    lfd = net->listen_fd;

    /*
     * shutdown() despierta al accept() bloqueado; close() por sí solo no lo
     * garantiza en todas las versiones del kernel. El descriptor se cierra
     * después del join para que el número no se reutilice mientras el hilo de
     * escucha todavía lo está usando.
     */
    if (lfd >= 0) {
        shutdown(lfd, SHUT_RDWR);
    }
    if (net->started) {
        pthread_join(net->listener_thread, NULL);
        net->started = 0;
    }
    if (lfd >= 0) {
        close(lfd);
        net->listen_fd = -1;
    }
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