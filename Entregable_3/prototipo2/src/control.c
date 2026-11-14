/*
 * control.c — Servidor de control para dsmctl (texto sobre TCP).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Protocolo mínimo: el cliente envía una línea con el comando y el servidor
 * responde texto plano y cierra. No hay autenticación porque es una herramienta
 * de laboratorio; el puerto de control no debe exponerse fuera de la red de
 * pruebas (se documenta como limitación).
 */
#include "dsm_control.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "dsm_coherence.h"
#include "dsm_heartbeat.h"
#include "dsm_memory.h"
#include "dsm_nodes.h"
#include "dsm_stats.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define DSM_CTL_LINE_MAX 512
#define DSM_CTL_TIMEOUT  5000u

struct dsm_control {
    dsm_runtime_t *rt;
    int            listen_fd;
    pthread_t      thread;
    _Atomic int    running;
    int            started;
};

static void ctl_timeout(int fd, uint32_t ms)
{
    struct timeval tv;

    tv.tv_sec  = (time_t)(ms / 1000u);
    tv.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int read_line(int fd, char *buf, size_t len)
{
    size_t used = 0;

    while (used + 1u < len) {
        char    c;
        ssize_t r = recv(fd, &c, 1, 0);

        if (r <= 0) {
            break;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        buf[used++] = c;
    }
    buf[used] = '\0';
    return (used > 0) ? 0 : -1;
}

static void write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t r = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);

        if (r <= 0) {
            break;
        }
        sent += (size_t)r;
    }
}

static int format_nodes(dsm_runtime_t *rt, char *out, size_t len)
{
    size_t   used = 0;
    uint32_t i;

    used += (size_t)snprintf(out + used, len - used,
                             "%-5s %-16s %-6s %-6s %s\n",
                             "nodo", "host", "dsm", "ctrl", "estado");
    if (used >= len) {
        return -1;
    }
    for (i = 0; i < DSM_MAX_PEERS; i++) {
        const dsm_peer_t *p = dsm_nodes_get(&rt->nodes, i);
        int               n;
        const char       *estado;

        if (p == NULL) {
            continue;
        }
        if (i == rt->cfg.node_id) {
            estado = "local";
        } else if (rt->hb != NULL) {
            estado = dsm_heartbeat_alive(rt->hb, i) ? "vivo" : "sin respuesta";
        } else {
            estado = "desconocido";
        }
        n = snprintf(out + used, len - used, "%-5u %-16s %-6u %-6u %s\n",
                     p->node_id, p->host, (unsigned)p->port,
                     (unsigned)(p->port + 1u), estado);
        if (n < 0 || (size_t)n >= len - used) {
            return -1;
        }
        used += (size_t)n;
    }
    return (int)used;
}

int dsm_control_dispatch(dsm_runtime_t *rt, const char *cmd, char *out, size_t len)
{
    if (rt == NULL || cmd == NULL || out == NULL || len == 0) {
        return -1;
    }
    out[0] = '\0';

    if (strcmp(cmd, "stats") == 0) {
        dsm_runtime_refresh_stats(rt);
        return dsm_stats_format(&rt->stats, out, len);
    }
    if (strcmp(cmd, "pages") == 0) {
        return dsm_coherence_format_pages(&rt->co, out, len);
    }
    if (strcmp(cmd, "dir") == 0) {
        return dsm_coherence_format_dir(&rt->co, out, len);
    }
    if (strcmp(cmd, "objects") == 0) {
        return dsm_coherence_format_objects(&rt->co, out, len);
    }
    if (strcmp(cmd, "nodes") == 0) {
        return format_nodes(rt, out, len);
    }
    if (strcmp(cmd, "info") == 0) {
        return snprintf(out, len,
                        "nodo          : %u\n"
                        "nodos         : %u\n"
                        "pagina        : %u bytes\n"
                        "ventana       : %llu bytes\n"
                        "marcos        : %llu\n"
                        "swap          : %s\n",
                        rt->cfg.node_id, rt->n_nodes,
                        dsm_memory_page_size(&rt->mem),
                        (unsigned long long)dsm_memory_window_size(&rt->mem),
                        (unsigned long long)(rt->cfg.node_memory /
                                             rt->cfg.page_size),
                        dsm_memory_swap_enabled(&rt->mem) ? "activo" : "apagado");
    }
    if (strcmp(cmd, "ping") == 0) {
        return snprintf(out, len, "pong %u\n", rt->cfg.node_id);
    }
    if (strcmp(cmd, "help") == 0) {
        return snprintf(out, len,
                        "comandos: stats pages dir objects nodes info ping help\n");
    }
    return snprintf(out, len, "error: comando desconocido: %s\n", cmd);
}

static void *control_loop(void *arg)
{
    dsm_control_t *ctl = arg;

    while (atomic_load_explicit(&ctl->running, memory_order_acquire)) {
        struct sockaddr_in addr;
        socklen_t          alen = sizeof(addr);
        int                cfd = accept(ctl->listen_fd, (struct sockaddr *)&addr, &alen);
        char               line[DSM_CTL_LINE_MAX];
        char               out[16384];
        int                n;

        if (cfd < 0) {
            if (!atomic_load_explicit(&ctl->running, memory_order_acquire)) {
                break;
            }
            continue;
        }
        ctl_timeout(cfd, DSM_CTL_TIMEOUT);
        if (read_line(cfd, line, sizeof(line)) != 0) {
            close(cfd);
            continue;
        }
        n = dsm_control_dispatch(ctl->rt, line, out, sizeof(out));
        if (n < 0) {
            write_all(cfd, "error\n", 6);
        } else {
            write_all(cfd, out, (size_t)n);
        }
        close(cfd);
    }
    return NULL;
}

dsm_control_t *dsm_control_start(dsm_runtime_t *rt)
{
    dsm_control_t     *ctl;
    struct sockaddr_in addr;
    int                opt = 1;

    if (rt == NULL || rt->cfg.control_port == 0) {
        return NULL;
    }
    ctl = calloc(1, sizeof(*ctl));
    if (ctl == NULL) {
        return NULL;
    }
    ctl->rt        = rt;
    ctl->listen_fd = -1;
    atomic_init(&ctl->running, 0);

    ctl->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctl->listen_fd < 0) {
        free(ctl);
        return NULL;
    }
    setsockopt(ctl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(rt->cfg.control_port);
    if (bind(ctl->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(ctl->listen_fd, 8) != 0) {
        close(ctl->listen_fd);
        free(ctl);
        return NULL;
    }
    atomic_store_explicit(&ctl->running, 1, memory_order_release);
    if (pthread_create(&ctl->thread, NULL, control_loop, ctl) != 0) {
        atomic_store_explicit(&ctl->running, 0, memory_order_release);
        close(ctl->listen_fd);
        free(ctl);
        return NULL;
    }
    ctl->started = 1;
    return ctl;
}

void dsm_control_stop(dsm_control_t *ctl)
{
    if (ctl == NULL) {
        return;
    }
    atomic_store_explicit(&ctl->running, 0, memory_order_release);
    if (ctl->listen_fd >= 0) {
        shutdown(ctl->listen_fd, SHUT_RDWR);
    }
    if (ctl->started) {
        pthread_join(ctl->thread, NULL);
        ctl->started = 0;
    }
    if (ctl->listen_fd >= 0) {
        close(ctl->listen_fd);
        ctl->listen_fd = -1;
    }
    free(ctl);
}
