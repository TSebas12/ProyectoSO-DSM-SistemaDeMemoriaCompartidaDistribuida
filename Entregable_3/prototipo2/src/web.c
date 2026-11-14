/*
 * web.c — Panel web mínimo de solo lectura.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Sirve GET / (HTML) y GET /stats.json (JSON). No acepta métodos de escritura
 * ni formularios: el panel es solo para observar el nodo mientras corre una
 * aplicación. Se eligió un servidor secuencial de un hilo porque las consultas
 * son esporádicas y así no se agrega concurrencia innecesaria al prototipo.
 */
#include "dsm_web.h"

#include <arpa/inet.h>
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
#include "dsm_memory.h"
#include "dsm_nodes.h"
#include "dsm_stats.h"
#include "dsm_sync.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define DSM_WEB_REQ_MAX  2048u
#define DSM_WEB_BODY_MAX 32768u
#define DSM_WEB_TIMEOUT  5000u

struct dsm_web {
    dsm_runtime_t *rt;
    int            listen_fd;
    pthread_t      thread;
    _Atomic int    running;
    int            started;
};

static void web_timeout(int fd, uint32_t ms)
{
    struct timeval tv;

    tv.tv_sec  = (time_t)(ms / 1000u);
    tv.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static void web_write(int fd, const char *buf, size_t len)
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

int dsm_web_render_json(dsm_runtime_t *rt, char *out, size_t len)
{
    size_t   used = 0;
    int      i;
    uint32_t k;

    if (rt == NULL || out == NULL || len == 0) {
        return -1;
    }
    dsm_runtime_refresh_stats(rt);
    used += (size_t)snprintf(out + used, len - used,
                             "{\"node\":%u,\"nodes\":%u,\"page_size\":%u,"
                             "\"resident\":%llu,\"frames_free\":%llu,"
                             "\"stats\":{",
                             rt->cfg.node_id, rt->n_nodes,
                             dsm_memory_page_size(&rt->mem),
                             (unsigned long long)dsm_stats_get(&rt->stats,
                                                               DSM_STAT_PAGES_RESIDENT),
                             (unsigned long long)dsm_stats_get(&rt->stats,
                                                               DSM_STAT_FRAMES_FREE));
    if (used >= len) {
        return -1;
    }
    for (i = 0; i < DSM_STATS_FIELD_COUNT; i++) {
        int n = snprintf(out + used, len - used, "%s\"%s\":%llu",
                         (i == 0) ? "" : ",",
                         dsm_stats_field_name((dsm_stats_field_t)i),
                         (unsigned long long)dsm_stats_get(&rt->stats,
                                                           (dsm_stats_field_t)i));
        if (n < 0 || (size_t)n >= len - used) {
            return -1;
        }
        used += (size_t)n;
    }
    used += (size_t)snprintf(out + used, len - used, "},\"objects\":[");
    if (used >= len) {
        return -1;
    }
    if (dsm_mutex_lock(&rt->co.obj_lock) == DSM_OK) {
        for (k = 0; k < rt->co.object_count; k++) {
            int n = snprintf(out + used, len - used,
                             "%s{\"id\":%llu,\"page_base\":%llu,"
                             "\"pages\":%u,\"owner\":%u,\"tag\":%u}",
                             (k == 0) ? "" : ",",
                             (unsigned long long)rt->co.objects[k].object_id,
                             (unsigned long long)rt->co.objects[k].page_base,
                             rt->co.objects[k].page_count,
                             rt->co.objects[k].owner_node,
                             rt->co.objects[k].tag);
            if (n < 0 || (size_t)n >= len - used) {
                dsm_mutex_unlock(&rt->co.obj_lock);
                return -1;
            }
            used += (size_t)n;
        }
        dsm_mutex_unlock(&rt->co.obj_lock);
    }
    used += (size_t)snprintf(out + used, len - used, "],\"peers\":[");
    if (used >= len) {
        return -1;
    }
    {
        int first = 1;

        for (k = 0; k < DSM_MAX_PEERS; k++) {
            const dsm_peer_t *p = dsm_nodes_get(&rt->nodes, k);
            int n;

            if (p == NULL) {
                continue;
            }
            n = snprintf(out + used, len - used,
                         "%s{\"id\":%u,\"host\":\"%s\",\"port\":%u}",
                         first ? "" : ",", p->node_id, p->host,
                         (unsigned)p->port);
            if (n < 0 || (size_t)n >= len - used) {
                return -1;
            }
            used += (size_t)n;
            first = 0;
        }
    }
    used += (size_t)snprintf(out + used, len - used, "]}\n");
    return (used < len) ? (int)used : -1;
}

int dsm_web_render_html(dsm_runtime_t *rt, char *out, size_t len)
{
    char     stats[4096];
    char     objects[4096];
    char     nodes[2048];
    int      n;

    if (rt == NULL || out == NULL || len == 0) {
        return -1;
    }
    dsm_runtime_refresh_stats(rt);
    if (dsm_stats_format_kv(&rt->stats, stats, sizeof(stats)) < 0) {
        stats[0] = '\0';
    }
    if (dsm_coherence_format_objects(&rt->co, objects, sizeof(objects)) < 0) {
        objects[0] = '\0';
    }
    {
        size_t nu = 0;

        nodes[0] = '\0';
        for (uint32_t k = 0; k < DSM_MAX_PEERS; k++) {
            const dsm_peer_t *p = dsm_nodes_get(&rt->nodes, k);

            if (p == NULL) {
                continue;
            }
            n = snprintf(nodes + nu, sizeof(nodes) - nu,
                         "%u %s:%u%s\n", p->node_id, p->host,
                         (unsigned)p->port,
                         (p->node_id == rt->cfg.node_id) ? "  (local)" : "");
            if (n < 0 || (size_t)n >= sizeof(nodes) - nu) {
                break;
            }
            nu += (size_t)n;
        }
    }

    n = snprintf(out, len,
                 "<!doctype html>\n"
                 "<html lang=\"es\"><head><meta charset=\"utf-8\">\n"
                 "<meta http-equiv=\"refresh\" content=\"2\">\n"
                 "<title>DSM nodo %u</title>\n"
                 "<style>body{font-family:system-ui,sans-serif;margin:2rem;"
                 "background:#0f172a;color:#e2e8f0}"
                 "h1,h2{color:#7dd3fc}pre{background:#1e293b;padding:1rem;"
                 "border-radius:8px;overflow:auto}"
                 "table{border-collapse:collapse}td,th{padding:.2rem .6rem;"
                 "border-bottom:1px solid #334155}</style></head><body>\n"
                 "<h1>Sistema de Memoria Compartida Distribuida</h1>\n"
                 "<p>Nodo <b>%u</b> de %u &mdash; <a href=\"/stats.json\" "
                 "style=\"color:#7dd3fc\">/stats.json</a></p>\n"
                 "<h2>Nodos</h2><pre>%s</pre>\n"
                 "<h2>Contadores</h2><pre>%s</pre>\n"
                 "<h2>Objetos</h2><pre>%s</pre>\n"
                 "</body></html>\n",
                 rt->cfg.node_id, rt->cfg.node_id, rt->n_nodes,
                 nodes, stats, objects);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

static void respond(int fd, int code, const char *type,
                    const char *body, size_t body_len)
{
    char   head[256];
    int    n;
    const char *reason = (code == 200) ? "OK" : "Not Found";

    n = snprintf(head, sizeof(head),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n\r\n",
                 code, reason, type, body_len);
    if (n > 0) {
        web_write(fd, head, (size_t)n);
    }
    if (body != NULL && body_len > 0) {
        web_write(fd, body, body_len);
    }
}

static void *web_loop(void *arg)
{
    dsm_web_t *web = arg;

    while (atomic_load_explicit(&web->running, memory_order_acquire)) {
        int  cfd = accept(web->listen_fd, NULL, NULL);
        char req[DSM_WEB_REQ_MAX];
        char body[DSM_WEB_BODY_MAX];
        char path[256];
        ssize_t got;
        int     n;

        if (cfd < 0) {
            if (!atomic_load_explicit(&web->running, memory_order_acquire)) {
                break;
            }
            continue;
        }
        web_timeout(cfd, DSM_WEB_TIMEOUT);
        got = recv(cfd, req, sizeof(req) - 1u, 0);
        if (got <= 0) {
            close(cfd);
            continue;
        }
        req[got] = '\0';
        path[0] = '\0';
        if (sscanf(req, "GET %255s", path) != 1) {
            respond(cfd, 404, "text/plain", "not found\n", 10);
            close(cfd);
            continue;
        }
        if (strcmp(path, "/stats.json") == 0) {
            n = dsm_web_render_json(web->rt, body, sizeof(body));
            if (n < 0) {
                respond(cfd, 404, "text/plain", "error\n", 6);
            } else {
                respond(cfd, 200, "application/json", body, (size_t)n);
            }
        } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            n = dsm_web_render_html(web->rt, body, sizeof(body));
            if (n < 0) {
                respond(cfd, 404, "text/plain", "error\n", 6);
            } else {
                respond(cfd, 200, "text/html; charset=utf-8", body, (size_t)n);
            }
        } else {
            respond(cfd, 404, "text/plain", "not found\n", 10);
        }
        close(cfd);
    }
    return NULL;
}

dsm_web_t *dsm_web_start(dsm_runtime_t *rt)
{
    dsm_web_t         *web;
    struct sockaddr_in addr;
    int                opt = 1;

    if (rt == NULL || rt->cfg.web_port == 0) {
        return NULL;
    }
    web = calloc(1, sizeof(*web));
    if (web == NULL) {
        return NULL;
    }
    web->rt        = rt;
    web->listen_fd = -1;
    atomic_init(&web->running, 0);

    web->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (web->listen_fd < 0) {
        free(web);
        return NULL;
    }
    setsockopt(web->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(rt->cfg.web_port);
    if (bind(web->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(web->listen_fd, 8) != 0) {
        close(web->listen_fd);
        free(web);
        return NULL;
    }
    atomic_store_explicit(&web->running, 1, memory_order_release);
    if (pthread_create(&web->thread, NULL, web_loop, web) != 0) {
        atomic_store_explicit(&web->running, 0, memory_order_release);
        close(web->listen_fd);
        free(web);
        return NULL;
    }
    web->started = 1;
    return web;
}

void dsm_web_stop(dsm_web_t *web)
{
    if (web == NULL) {
        return;
    }
    atomic_store_explicit(&web->running, 0, memory_order_release);
    if (web->listen_fd >= 0) {
        shutdown(web->listen_fd, SHUT_RDWR);
    }
    if (web->started) {
        pthread_join(web->thread, NULL);
        web->started = 0;
    }
    if (web->listen_fd >= 0) {
        close(web->listen_fd);
        web->listen_fd = -1;
    }
    free(web);
}
