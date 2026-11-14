/*
 * net.c — Transporte TCP con conexiones persistentes y pool de hilos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Modelo:
 *   - Cada nodo escucha en su listen_port. El hilo aceptador mete los
 *     descriptores en una cola y un pool de trabajadores atiende cada conexión
 *     en un bucle: leer mensaje, procesarlo, responder por el mismo socket.
 *     La conexión queda abierta hasta que el peer la cierre o el nodo se apague.
 *   - Cada par de nodos mantiene UNA conexión saliente reutilizable, protegida
 *     por un mutex. Un RPC toma el mutex, escribe, lee la respuesta y lo suelta.
 *   - Si un RPC falla, se cierra la conexión, se reintenta con backoff y se
 *     contabiliza el reintento. Un timeout deja la conexión inservible (pudo
 *     quedar un mensaje a medias), por eso siempre se cierra.
 */
#include "dsm_log.h"
#include "dsm_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dsm_protocol.h"
#include "dsm_sync.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define DSM_NET_MAX_WORKERS  32
#define DSM_NET_QUEUE_LEN    64
#define DSM_NET_MAX_ACTIVE   64
#define DSM_NET_IDLE_MS      60000u

typedef struct dsm_peer_conn {
    int         fd;
    dsm_mutex_t lock;
    int         lock_ready;
} dsm_peer_conn_t;

struct dsm_net {
    dsm_config_t     cfg;
    dsm_nodes_t      nodes;
    dsm_coherence_t *co;
    dsm_stats_t     *stats;

    dsm_peer_conn_t  conn[DSM_MAX_PEERS];

    int              listen_fd;
    pthread_t        listener_thread;
    pthread_t        workers[DSM_NET_MAX_WORKERS];
    uint32_t         worker_count;
    int              started;

    int              queue[DSM_NET_QUEUE_LEN];
    uint32_t         q_head;
    uint32_t         q_tail;
    uint32_t         q_count;
    dsm_mutex_t      q_lock;
    dsm_cond_t       q_cond;
    int              q_ready;

    int              active[DSM_NET_MAX_ACTIVE];
    dsm_mutex_t      active_lock;
    int              active_ready;

    _Atomic int      running;
};

/*
 * Socket por el que debe salir la respuesta del mensaje que se está
 * atendiendo. Cada trabajador tiene el suyo porque el descriptor es distinto
 * por conexión.
 */
static _Thread_local int t_reply_fd = -1;

/* --- Utilidades --------------------------------------------------------- */
static uint64_t now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000L);
}

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

static void set_nodelay(int fd)
{
    int one = 1;

    /* Los mensajes son pequeños y sensibles a la latencia: sin Nagle.     */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
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

int dsm_net_read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;

    while (got < n) {
        ssize_t r = recv(fd, (char *)buf + got, n - got, 0);
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

/* --- Conexión saliente -------------------------------------------------- */
static int connect_with_timeout(const char *host, uint16_t port, uint32_t timeout_ms)
{
    struct sockaddr_in addr;
    struct pollfd      pfd;
    int                fd;
    int                rc;
    int                err = 0;
    socklen_t          elen = sizeof(err);
    int                flags;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        pfd.fd     = fd;
        pfd.events = POLLOUT;
        rc = poll(&pfd, 1, (int)((timeout_ms == 0) ? 2000u : timeout_ms));
        if (rc <= 0) {
            close(fd);
            return -1;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
            close(fd);
            return -1;
        }
    }
    if (fcntl(fd, F_SETFL, flags) < 0) {
        close(fd);
        return -1;
    }
    set_nodelay(fd);
    return fd;
}

/* Toma el lock del peer y garantiza que haya conexión. Devuelve fd o -1. */
static int peer_acquire(dsm_net_t *net, uint32_t node, uint32_t timeout_ms)
{
    dsm_peer_conn_t *c;
    const dsm_peer_t *peer;

    if (net == NULL || node >= DSM_MAX_PEERS || node == net->cfg.node_id) {
        return -1;
    }
    peer = dsm_nodes_get(&net->nodes, node);
    if (peer == NULL) {
        DSM_LOG(DSM_LOG_DEBUG, "peer_acquire: no peer info for node %u", node);
        return -1;
    }
    c = &net->conn[node];
    if (!c->lock_ready) {
        DSM_LOG(DSM_LOG_DEBUG, "peer_acquire: lock not ready for node %u", node);
        return -1;
    }
    if (dsm_mutex_lock(&c->lock) != DSM_OK) {
        DSM_LOG(DSM_LOG_DEBUG, "peer_acquire: mutex lock failed for node %u", node);
        return -1;
    }
    if (c->fd < 0) {
        DSM_LOG(DSM_LOG_DEBUG, "peer_acquire: connecting to node %u at %s:%u",
                node, peer->host, (unsigned)peer->port);
        c->fd = connect_with_timeout(peer->host, peer->port,
                                     net->cfg.connect_timeout_ms);
        if (c->fd < 0) {
            DSM_LOG(DSM_LOG_DEBUG, "peer_acquire: connect failed for node %u", node);
            dsm_mutex_unlock(&c->lock);
            return -1;
        }
    }
    set_timeouts(c->fd, (timeout_ms == 0) ? net->cfg.network_timeout_ms : timeout_ms);
    return c->fd;
}

static void peer_release(dsm_net_t *net, uint32_t node, int keep)
{
    dsm_peer_conn_t *c;

    if (net == NULL || node >= DSM_MAX_PEERS) {
        return;
    }
    c = &net->conn[node];
    if (!c->lock_ready) {
        return;
    }
    if (!keep && c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    dsm_mutex_unlock(&c->lock);
}

/* --- RPC ---------------------------------------------------------------- */
static dsm_status_t rpc_once(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req,
                             dsm_msg_t *resp, uint32_t timeout_ms)
{
    int      fd;
    uint8_t  hdr[64];
    dsm_msg_header_t h;
    uint8_t *rbuf;
    size_t   total;
    dsm_status_t st = DSM_OK;

    fd = peer_acquire(net, dst, timeout_ms);
    if (fd < 0) {
        return DSM_ERR_PEER_DOWN;
    }
    if (write_msg_fd(net, fd, req) != 0) {
        peer_release(net, dst, 0);
        return DSM_ERR_NET;
    }
    if (dsm_net_read_full(fd, hdr, 64) != 0) {
        peer_release(net, dst, 0);
        return DSM_ERR_TIMEOUT;
    }
    dsm_header_decode(hdr, &h);
    if (h.magic != DSM_MAGIC || h.version != DSM_PROTOCOL_VERSION) {
        peer_release(net, dst, 0);
        return DSM_ERR_PROTOCOL;
    }
    if (h.payload_len > net->cfg.max_message_size) {
        peer_release(net, dst, 0);
        return DSM_ERR_PROTOCOL;
    }
    total = 64u + h.payload_len;
    rbuf  = malloc(total);
    if (rbuf == NULL) {
        peer_release(net, dst, 0);
        return DSM_ERR_NOMEM;
    }
    memcpy(rbuf, hdr, 64);
    if (h.payload_len > 0 &&
        dsm_net_read_full(fd, rbuf + 64, h.payload_len) != 0) {
        free(rbuf);
        peer_release(net, dst, 0);
        return DSM_ERR_TIMEOUT;
    }
    if (dsm_protocol_decode(rbuf, total, resp) != 0) {
        free(rbuf);
        peer_release(net, dst, 0);
        return DSM_ERR_CHECKSUM;
    }
    note_received(net, total);
    free(rbuf);
    peer_release(net, dst, 1);
    return st;
}

static dsm_status_t rpc(dsm_net_t *net, uint32_t dst, const dsm_msg_t *req,
                        dsm_msg_t *resp, uint32_t timeout_ms)
{
    dsm_status_t st = DSM_ERR_NET;
    uint32_t     attempt;
    uint32_t     max_attempts;

    if (net == NULL || req == NULL || resp == NULL) {
        return DSM_ERR_INVAL;
    }
    max_attempts = net->cfg.retry_count + 1u;
    for (attempt = 0; attempt < max_attempts; attempt++) {
        uint64_t t0 = now_us();

        memset(resp, 0, sizeof(*resp));
        st = rpc_once(net, dst, req, resp, timeout_ms);
        if (net->stats != NULL) {
            dsm_stats_inc(net->stats, DSM_STAT_RPC_COUNT);
            dsm_stats_add(net->stats, DSM_STAT_RPC_LATENCY_US, now_us() - t0);
        }
        if (st == DSM_OK) {
            return DSM_OK;
        }
        if (resp->data != NULL) {
            free((void *)resp->data);
            resp->data = NULL;
        }
        if (net->stats != NULL) {
            if (st == DSM_ERR_TIMEOUT) {
                dsm_stats_inc(net->stats, DSM_STAT_TIMEOUTS);
            } else {
                dsm_stats_inc(net->stats, DSM_STAT_ERRORS);
            }
            if (attempt + 1u < max_attempts) {
                dsm_stats_inc(net->stats, DSM_STAT_RETRIES);
            }
        }
        if (attempt + 1u < max_attempts) {
            uint32_t backoff = net->cfg.retry_backoff_ms * (attempt + 1u);
            struct timespec ts;

            ts.tv_sec  = (time_t)(backoff / 1000u);
            ts.tv_nsec = (long)(backoff % 1000u) * 1000000L;
            nanosleep(&ts, NULL);
        }
    }
    return st;
}

/* --- Callbacks de coherencia -------------------------------------------- */
dsm_status_t dsm_net_send(void *ctx, const dsm_msg_t *msg)
{
    dsm_net_t *net = (dsm_net_t *)ctx;
    int        fd;

    if (net == NULL || msg == NULL) {
        return DSM_ERR_INVAL;
    }
    /*
     * Dentro de un trabajador, la respuesta pertenece al RPC que llegó por
     * t_reply_fd. Fuera de él, es un envío saliente normal por la conexión
     * persistente hacia el destino.
     */
    if (t_reply_fd >= 0) {
        return (write_msg_fd(net, t_reply_fd, msg) == 0) ? DSM_OK : DSM_ERR_NET;
    }
    fd = peer_acquire(net, msg->dst_node, 0);
    if (fd < 0) {
        return DSM_ERR_PEER_DOWN;
    }
    if (write_msg_fd(net, fd, msg) != 0) {
        peer_release(net, msg->dst_node, 0);
        return DSM_ERR_NET;
    }
    peer_release(net, msg->dst_node, 1);
    return DSM_OK;
}

dsm_status_t dsm_net_call(void *ctx, const dsm_msg_t *req, dsm_msg_t *resp,
                          uint32_t timeout_ms)
{
    dsm_net_t *net = (dsm_net_t *)ctx;

    if (net == NULL) {
        return DSM_ERR_INVAL;
    }
    return rpc(net, req->dst_node, req, resp,
               (timeout_ms == 0) ? net->cfg.network_timeout_ms : timeout_ms);
}

dsm_coherence_ops_t dsm_net_ops(dsm_net_t *net)
{
    dsm_coherence_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    ops.ctx  = net;
    ops.send = dsm_net_send;
    ops.call = dsm_net_call;
    return ops;
}

/* --- Servidor ----------------------------------------------------------- */
static void active_add(dsm_net_t *net, int fd)
{
    uint32_t i;

    if (!net->active_ready) {
        return;
    }
    if (dsm_mutex_lock(&net->active_lock) != DSM_OK) {
        return;
    }
    for (i = 0; i < DSM_NET_MAX_ACTIVE; i++) {
        if (net->active[i] < 0) {
            net->active[i] = fd;
            break;
        }
    }
    dsm_mutex_unlock(&net->active_lock);
}

static void active_remove(dsm_net_t *net, int fd)
{
    uint32_t i;

    if (!net->active_ready) {
        return;
    }
    if (dsm_mutex_lock(&net->active_lock) != DSM_OK) {
        return;
    }
    for (i = 0; i < DSM_NET_MAX_ACTIVE; i++) {
        if (net->active[i] == fd) {
            net->active[i] = -1;
            break;
        }
    }
    dsm_mutex_unlock(&net->active_lock);
}

static void reply_simple(dsm_net_t *net, const dsm_msg_t *msg, uint16_t type,
                         const void *data, uint32_t len, dsm_status_t code)
{
    dsm_msg_t resp;

    memset(&resp, 0, sizeof(resp));
    resp.type       = type;
    resp.flags      = DSM_FLAG_RESPONSE;
    resp.src_node   = net->cfg.node_id;
    resp.dst_node   = msg->src_node;
    resp.request_id = msg->request_id;
    resp.page_id    = msg->page_id;
    resp.status     = (int32_t)code;
    resp.data       = (const uint8_t *)data;
    resp.data_len   = len;
    (void)dsm_net_send(net, &resp);
}

/* Atiende una conexión persistente hasta que el peer la cierre. */
static void serve_connection(dsm_net_t *net, int cfd)
{
    active_add(net, cfd);
    set_timeouts(cfd, DSM_NET_IDLE_MS);

    while (atomic_load_explicit(&net->running, memory_order_acquire)) {
        uint8_t          hdr[64];
        dsm_msg_header_t h;
        uint8_t         *buf;
        size_t           total;
        dsm_msg_t        msg;

        if (dsm_net_read_full(cfd, hdr, 64) != 0) {
            break;
        }
        dsm_header_decode(hdr, &h);
        if (h.magic != DSM_MAGIC || h.version != DSM_PROTOCOL_VERSION ||
            h.payload_len > net->cfg.max_message_size) {
            break;
        }
        total = 64u + h.payload_len;
        buf   = malloc(total);
        if (buf == NULL) {
            break;
        }
        memcpy(buf, hdr, 64);
        if (h.payload_len > 0 &&
            dsm_net_read_full(cfd, buf + 64, h.payload_len) != 0) {
            free(buf);
            break;
        }
        memset(&msg, 0, sizeof(msg));
        if (dsm_protocol_decode(buf, total, &msg) != 0) {
            free(buf);
            continue;
        }
        note_received(net, total);

        /* Mensajes de servicio que no pasan por la coherencia. */
        if (msg.type == DSM_MSG_PING) {
            reply_simple(net, &msg, DSM_MSG_PONG, NULL, 0, DSM_OK);
        } else if (msg.type == DSM_MSG_STATS_REQUEST) {
            dsm_stats_wire_t wire;
            dsm_stats_view_t view;

            memset(&wire, 0, sizeof(wire));
            wire.node_id     = net->cfg.node_id;
            wire.field_count = DSM_STATS_FIELD_COUNT;
            if (net->stats != NULL) {
                dsm_stats_snapshot(net->stats, &view);
                memcpy(wire.field, view.field, sizeof(wire.field));
            }
            reply_simple(net, &msg, DSM_MSG_STATS_REPLY, &wire,
                         (uint32_t)sizeof(wire), DSM_OK);
        } else {
            /*
             * La coherencia responde con ops.send(); ese envío debe volver por
             * esta misma conexión para que el RPC del solicitante lo lea.
             */
            t_reply_fd = cfd;
            (void)dsm_coherence_handle_message(net->co, &msg);
            t_reply_fd = -1;
        }

        if (msg.data != NULL) {
            free((void *)msg.data);
        }
        free(buf);
    }

    active_remove(net, cfd);
}

static int queue_pop(dsm_net_t *net)
{
    int fd = -1;

    if (dsm_mutex_lock(&net->q_lock) != DSM_OK) {
        return -1;
    }
    while (net->q_count == 0 &&
           atomic_load_explicit(&net->running, memory_order_acquire)) {
        if (dsm_cond_wait(&net->q_cond, &net->q_lock) != DSM_OK) {
            break;
        }
    }
    if (net->q_count > 0) {
        fd = net->queue[net->q_tail];
        net->q_tail = (net->q_tail + 1u) % DSM_NET_QUEUE_LEN;
        net->q_count--;
    }
    dsm_mutex_unlock(&net->q_lock);
    return fd;
}

static int queue_push(dsm_net_t *net, int fd)
{
    if (dsm_mutex_lock(&net->q_lock) != DSM_OK) {
        return -1;
    }
    if (net->q_count >= DSM_NET_QUEUE_LEN) {
        dsm_mutex_unlock(&net->q_lock);
        return -1;
    }
    net->queue[net->q_head] = fd;
    net->q_head = (net->q_head + 1u) % DSM_NET_QUEUE_LEN;
    net->q_count++;
    dsm_cond_signal(&net->q_cond);
    dsm_mutex_unlock(&net->q_lock);
    return 0;
}

static void *worker_loop(void *arg)
{
    dsm_net_t *net = arg;

    for (;;) {
        int fd = queue_pop(net);

        if (fd < 0) {
            break;
        }
        serve_connection(net, fd);
        close(fd);
    }
    return NULL;
}

static void *listener_loop(void *arg)
{
    dsm_net_t *net = arg;
    int        lfd = net->listen_fd;

    while (atomic_load_explicit(&net->running, memory_order_acquire)) {
        int cfd = accept(lfd, NULL, NULL);

        if (cfd < 0) {
            if (!atomic_load_explicit(&net->running, memory_order_acquire)) {
                break;
            }
            continue;
        }
        set_nodelay(cfd);
        if (queue_push(net, cfd) != 0) {
            close(cfd);
        }
    }
    return NULL;
}

/* --- Ciclo de vida ------------------------------------------------------ */
dsm_net_t *dsm_net_create(const dsm_config_t *cfg, const dsm_nodes_t *nodes,
                          dsm_coherence_t *co)
{
    dsm_net_t *net;
    uint32_t   i;

    if (cfg == NULL || nodes == NULL) {
        return NULL;
    }
    net = calloc(1, sizeof(*net));
    if (net == NULL) {
        return NULL;
    }
    net->cfg      = *cfg;
    net->nodes    = *nodes;
    net->co       = co;
    net->listen_fd = -1;
    atomic_init(&net->running, 0);

    for (i = 0; i < DSM_MAX_PEERS; i++) {
        net->conn[i].fd = -1;
        net->conn[i].lock_ready = 0;
    }
    for (i = 0; i < DSM_NET_MAX_ACTIVE; i++) {
        net->active[i] = -1;
    }
    return net;
}

void dsm_net_set_stats(dsm_net_t *net, dsm_stats_t *stats)
{
    if (net != NULL) {
        net->stats = stats;
    }
}

dsm_status_t dsm_net_start(dsm_net_t *net)
{
    struct sockaddr_in addr;
    int                opt = 1;
    uint32_t           i;

    if (net == NULL) {
        return DSM_ERR_INVAL;
    }

    /* Locks y conexiones por peer. */
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        if (dsm_mutex_init(&net->conn[i].lock, DSM_LOCK_GLOBAL, "conexion") != DSM_OK) {
            return DSM_ERR_NOMEM;
        }
        net->conn[i].lock_ready = 1;
    }
    if (dsm_mutex_init(&net->q_lock, DSM_LOCK_GLOBAL, "cola_red") != DSM_OK) {
        return DSM_ERR_NOMEM;
    }
    if (dsm_cond_init(&net->q_cond) != DSM_OK) {
        return DSM_ERR_NOMEM;
    }
    net->q_ready = 1;
    if (dsm_mutex_init(&net->active_lock, DSM_LOCK_GLOBAL, "conexiones") != DSM_OK) {
        return DSM_ERR_NOMEM;
    }
    net->active_ready = 1;

    net->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (net->listen_fd < 0) {
        return DSM_ERR_NET;
    }
    setsockopt(net->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, net->cfg.bind_address[0] ? net->cfg.bind_address : "0.0.0.0",
              &addr.sin_addr);
    addr.sin_port = htons(net->cfg.listen_port);
    if (bind(net->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        return DSM_ERR_NET;
    }
    if (listen(net->listen_fd, 128) != 0) {
        return DSM_ERR_NET;
    }

    atomic_store_explicit(&net->running, 1, memory_order_release);

    net->worker_count = net->cfg.thread_pool_size;
    if (net->worker_count < 4u) {
        net->worker_count = 4u;
    }
    if (net->worker_count > DSM_NET_MAX_WORKERS) {
        net->worker_count = DSM_NET_MAX_WORKERS;
    }
    for (i = 0; i < net->worker_count; i++) {
        if (pthread_create(&net->workers[i], NULL, worker_loop, net) != 0) {
            net->worker_count = i;
            break;
        }
    }
    if (net->worker_count == 0) {
        atomic_store_explicit(&net->running, 0, memory_order_release);
        return DSM_ERR_NET;
    }
    if (pthread_create(&net->listener_thread, NULL, listener_loop, net) != 0) {
        atomic_store_explicit(&net->running, 0, memory_order_release);
        return DSM_ERR_NET;
    }
    net->started = 1;
    return DSM_OK;
}

void dsm_net_close_peers(dsm_net_t *net)
{
    uint32_t i;

    if (net == NULL) {
        return;
    }
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        if (!net->conn[i].lock_ready) {
            continue;
        }
        if (dsm_mutex_lock(&net->conn[i].lock) == DSM_OK) {
            if (net->conn[i].fd >= 0) {
                close(net->conn[i].fd);
                net->conn[i].fd = -1;
            }
            dsm_mutex_unlock(&net->conn[i].lock);
        }
    }
}

void dsm_net_stop(dsm_net_t *net)
{
    uint32_t i;

    if (net == NULL) {
        return;
    }
    atomic_store_explicit(&net->running, 0, memory_order_release);

    if (net->listen_fd >= 0) {
        shutdown(net->listen_fd, SHUT_RDWR);
    }
    if (net->active_ready &&
        dsm_mutex_lock(&net->active_lock) == DSM_OK) {
        for (i = 0; i < DSM_NET_MAX_ACTIVE; i++) {
            if (net->active[i] >= 0) {
                shutdown(net->active[i], SHUT_RDWR);
            }
        }
        dsm_mutex_unlock(&net->active_lock);
    }
    if (net->q_ready && dsm_mutex_lock(&net->q_lock) == DSM_OK) {
        dsm_cond_broadcast(&net->q_cond);
        dsm_mutex_unlock(&net->q_lock);
    }

    if (net->started) {
        pthread_join(net->listener_thread, NULL);
        for (i = 0; i < net->worker_count; i++) {
            pthread_join(net->workers[i], NULL);
        }
        net->started = 0;
    }
    if (net->listen_fd >= 0) {
        close(net->listen_fd);
        net->listen_fd = -1;
    }
    dsm_net_close_peers(net);
}

void dsm_net_destroy(dsm_net_t *net)
{
    uint32_t i;

    if (net == NULL) {
        return;
    }
    dsm_net_stop(net);
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        if (net->conn[i].lock_ready) {
            dsm_mutex_destroy(&net->conn[i].lock);
            net->conn[i].lock_ready = 0;
        }
    }
    if (net->q_ready) {
        dsm_cond_destroy(&net->q_cond);
        dsm_mutex_destroy(&net->q_lock);
        net->q_ready = 0;
    }
    if (net->active_ready) {
        dsm_mutex_destroy(&net->active_lock);
        net->active_ready = 0;
    }
    free(net);
}

/* --- Utilidades --------------------------------------------------------- */
dsm_status_t dsm_net_ping(dsm_net_t *net, uint32_t node_id, uint32_t timeout_ms)
{
    dsm_msg_t req;
    dsm_msg_t resp;
    dsm_status_t st;

    if (net == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(&req, 0, sizeof(req));
    req.type     = DSM_MSG_PING;
    req.src_node = net->cfg.node_id;
    req.dst_node = node_id;
    req.page_id  = DSM_INVALID_PAGE_ID;

    memset(&resp, 0, sizeof(resp));
    st = rpc(net, node_id, &req, &resp, timeout_ms);
    if (resp.data != NULL) {
        free((void *)resp.data);
    }
    if (st != DSM_OK) {
        return st;
    }
    return (resp.type == DSM_MSG_PONG) ? DSM_OK : DSM_ERR_PROTOCOL;
}

dsm_status_t dsm_net_stats(dsm_net_t *net, uint32_t node_id,
                           dsm_stats_wire_t *out, uint32_t timeout_ms)
{
    dsm_msg_t req;
    dsm_msg_t resp;
    dsm_status_t st;

    if (net == NULL || out == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(&req, 0, sizeof(req));
    req.type     = DSM_MSG_STATS_REQUEST;
    req.src_node = net->cfg.node_id;
    req.dst_node = node_id;
    req.page_id  = DSM_INVALID_PAGE_ID;

    memset(&resp, 0, sizeof(resp));
    st = rpc(net, node_id, &req, &resp, timeout_ms);
    if (st == DSM_OK) {
        if (resp.type == DSM_MSG_STATS_REPLY &&
            resp.data_len >= sizeof(*out)) {
            memcpy(out, resp.data, sizeof(*out));
        } else {
            st = DSM_ERR_PROTOCOL;
        }
    }
    if (resp.data != NULL) {
        free((void *)resp.data);
    }
    return st;
}

const dsm_peer_t *dsm_net_peer(const dsm_net_t *net, uint32_t node_id)
{
    if (net == NULL) {
        return NULL;
    }
    return dsm_nodes_get(&net->nodes, node_id);
}

uint32_t dsm_net_node_count(const dsm_net_t *net)
{
    return (net == NULL) ? 0u : net->nodes.count;
}
