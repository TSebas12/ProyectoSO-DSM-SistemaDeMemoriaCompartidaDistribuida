/*
 * ctl_main.c — Cliente de control (dsmctl).
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Uso:
 *   ./dsmctl stats
 *   ./dsmctl --config config/node1.conf pages
 *   ./dsmctl --host 192.0.2.20 --port 27305 dir
 *
 * El cliente abre una conexión TCP, envía una línea y copia la respuesta a la
 * salida estándar. Si no se indica host/puerto, se toman del archivo de
 * configuración del nodo local; así `./dsmctl stats` funciona sin argumentos
 * dentro del directorio del proyecto.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "dsm_config.h"
#include "dsm_types.h"

#define CTL_CMD_MAX   512
#define CTL_REPLY_MAX 65536

static void usage(const char *prog)
{
    printf("uso: %s [opciones] COMANDO [ARG...]\n"
           "  --config RUTA   configuracion del nodo (def. config/node0.conf)\n"
           "  --host H        host del nodo de control\n"
           "  --port P        puerto de control\n"
           "  --timeout MS    tiempo limite de la conexion (def. 5000)\n"
           "\n"
           "comandos: stats pages dir objects nodes info ping help\n",
           prog);
}

/* Conecta con tiempo límite; devuelve el descriptor o -1.                 */
static int connect_to(const char *host, uint16_t port, uint32_t timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    struct timeval   tv;
    char             port_str[16];
    int              fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }
    for (it = res; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        tv.tv_sec  = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t r = send(fd, buf + sent, len - sent, 0);

        if (r <= 0) {
            return -1;
        }
        sent += (size_t)r;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char   *config_path = "config/node0.conf";
    const char   *host        = NULL;
    uint32_t      timeout_ms  = 5000u;
    uint16_t      port        = 0;
    dsm_config_t  cfg;
    char          cmd[CTL_CMD_MAX];
    char         *reply;
    size_t        used = 0;
    int           fd;
    int           i;

    memset(&cfg, 0, sizeof(cfg));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "opcion desconocida: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            break;
        }
    }

    if (i >= argc) {
        fprintf(stderr, "falta el comando\n");
        usage(argv[0]);
        return 2;
    }

    /* Si no se dio host/puerto, se leen del archivo del nodo local.       */
    if (host == NULL || port == 0) {
        if (dsm_config_load(config_path, &cfg) != DSM_OK) {
            fprintf(stderr, "no se pudo leer %s; use --host y --port\n",
                    config_path);
            return 1;
        }
        if (host == NULL) {
            host = "127.0.0.1";
        }
        if (port == 0) {
            port = cfg.control_port;
        }
    }

    /* Une los argumentos restantes con espacios.                         */
    cmd[0] = '\0';
    for (; i < argc; i++) {
        size_t need = strlen(cmd) + strlen(argv[i]) + 2u;

        if (need > sizeof(cmd)) {
            fprintf(stderr, "comando demasiado largo\n");
            return 2;
        }
        if (cmd[0] != '\0') {
            strcat(cmd, " ");
        }
        strcat(cmd, argv[i]);
    }

    fd = connect_to(host, port, timeout_ms);
    if (fd < 0) {
        fprintf(stderr, "no se pudo conectar a %s:%u (%s)\n",
                host, (unsigned)port, strerror(errno));
        return 1;
    }

    {
        char line[CTL_CMD_MAX + 2u];

        snprintf(line, sizeof(line), "%s\n", cmd);
        if (send_all(fd, line, strlen(line)) != 0) {
            fprintf(stderr, "no se pudo enviar el comando\n");
            close(fd);
            return 1;
        }
    }

    reply = malloc(CTL_REPLY_MAX);
    if (reply == NULL) {
        close(fd);
        return 1;
    }
    for (;;) {
        ssize_t r;

        if (used + 1u >= CTL_REPLY_MAX) {
            break;
        }
        r = recv(fd, reply + used, CTL_REPLY_MAX - used - 1u, 0);
        if (r <= 0) {
            break;
        }
        used += (size_t)r;
    }
    reply[used] = '\0';
    close(fd);

    fputs(reply, stdout);
    if (used > 0 && reply[used - 1u] != '\n') {
        fputc('\n', stdout);
    }
    free(reply);
    return 0;
}
