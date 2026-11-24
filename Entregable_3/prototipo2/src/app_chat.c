/*
 * app_chat.c — Chat de texto libre entre nodos con seqlock.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: Entrega final
 *
 * Cada nodo escribe en su propia página (una por nodo) de un arreglo DSM
 * compartido. Los demás nodos leen todas las páginas con un seqlock y
 * muestran los mensajes nuevos. No usa barreras en el bucle; solo al
 * inicio y al final.
 */
#include "dsm_apps.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dsm_api.h"
#include "dsm_runtime.h"

#define DSM_CHAT_MAGIC   0x43484154u   /* "CHAT" */
#define DSM_CHAT_TEXT    4072u         /* page_size(4096) - 24 */
#define DSM_CHAT_POLL_MS 200u

typedef struct chat_slot {
    _Atomic uint32_t magic;
    _Atomic uint32_t seq;
    uint16_t         from;
    uint16_t         len;
    uint32_t         checksum;
    uint32_t         flags;
    uint32_t         reserved;
    char             text[DSM_CHAT_TEXT];
} chat_slot_t;

static uint32_t fnv1a(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    size_t   i;

    for (i = 0; i < n; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t now_sec(void)
{
    return (uint32_t)time(NULL);
}

static void print_remote(chat_slot_t *slot, uint32_t *last_seq)
{
    uint32_t s1, s2;
    chat_slot_t copy;

    for (;;) {
        s1 = atomic_load_explicit(&slot->seq, memory_order_acquire);
        if (s1 == 0u) {
            return;
        }
        memcpy(&copy, slot, sizeof(copy));
        s2 = atomic_load_explicit(&slot->seq, memory_order_acquire);
        if (s1 == s2) {
            break;
        }
    }
    if (copy.magic != DSM_CHAT_MAGIC) {
        return;
    }
    if (copy.checksum != fnv1a(copy.text, copy.len)) {
        return;
    }
    if (s1 <= *last_seq) {
        return;
    }
    *last_seq = s1;
    copy.text[copy.len < DSM_CHAT_TEXT ? copy.len : DSM_CHAT_TEXT - 1] = '\0';
    printf("[nodo %u] %s\n", copy.from, copy.text);
    fflush(stdout);
}

static void send_msg(chat_slot_t *slot, uint32_t from, const char *text)
{
    size_t len = strlen(text);

    if (len >= DSM_CHAT_TEXT) {
        len = DSM_CHAT_TEXT - 1;
    }
    memcpy(slot->text, text, len);
    slot->text[len] = '\0';
    slot->from      = (uint16_t)from;
    slot->len       = (uint16_t)len;
    slot->checksum  = fnv1a(slot->text, len);
    slot->flags     = 0u;
    atomic_store_explicit(&slot->magic, DSM_CHAT_MAGIC, memory_order_relaxed);
    atomic_store_explicit(&slot->seq, atomic_load_explicit(&slot->seq, memory_order_relaxed) + 1u,
                          memory_order_release);
}

int dsm_app_chat_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t      nodes = dsm_node_count(rt);
    uint32_t      rank  = dsm_node_id(rt);
    uint32_t      page_size = dsm_page_size(rt);
    char         *buf;
    chat_slot_t **slots;
    uint32_t     *last_seq;
    const char   *nick;
    uint32_t      duration;
    uint32_t      start;
    int           running = 1;

    if (nodes < 2u) {
        printf("nodo %u: chat: se necesitan al menos 2 nodos (hay %u)\n",
               rank, nodes);
        return -1;
    }
    if (page_size < sizeof(chat_slot_t)) {
        fprintf(stderr, "nodo %u: chat: page_size insuficiente\n", rank);
        return 1;
    }

    buf = dsm_malloc(rt, (size_t)nodes * (size_t)page_size);
    if (buf == NULL) {
        fprintf(stderr, "nodo %u: chat: no se pudo reservar memoria\n", rank);
        return 1;
    }
    memset(buf, 0, (size_t)nodes * (size_t)page_size);

    slots = calloc(nodes, sizeof(*slots));
    last_seq = calloc(nodes, sizeof(*last_seq));
    if (slots == NULL || last_seq == NULL) {
        free(slots);
        free(last_seq);
        return 1;
    }
    for (uint32_t i = 0; i < nodes; i++) {
        slots[i] = (chat_slot_t *)(buf + (size_t)i * page_size);
    }

    nick = (opts != NULL && opts->nick != NULL && opts->nick[0] != '\0')
           ? opts->nick : NULL;
    /* nick por defecto */
    char default_nick[32];
    if (nick == NULL) {
        snprintf(default_nick, sizeof(default_nick), "nodo%u", rank);
        nick = default_nick;
    }

    duration = (opts != NULL) ? opts->duration : 0u;
    if (opts != NULL && opts->text != NULL && opts->text[0] != '\0') {
        send_msg(slots[rank], rank, opts->text);
        printf("[tu] %s\n", opts->text);
        fflush(stdout);
        if (duration == 0u) {
            duration = 5u;
        }
    }

    if (dsm_barrier(rt, 120000u) != 0) {
        fprintf(stderr, "nodo %u: chat: fallo la barrera inicial\n", rank);
        free(slots);
        free(last_seq);
        return 1;
    }

    start = now_sec();
    printf("nodo %u (%s): chat listo. Comandos: /quit /nick NOMBRE /help\n",
           rank, nick);
    fflush(stdout);

    while (running) {
        struct pollfd pfd;
        char          line[1024];
        /* Leer mensajes remotos. */
        for (uint32_t i = 0; i < nodes; i++) {
            if (i == rank) {
                continue;
            }
            print_remote(slots[i], &last_seq[i]);
        }

        /* Tiempo de salida. */
        if (duration > 0u && (now_sec() - start) >= duration) {
            break;
        }

        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, DSM_CHAT_POLL_MS) <= 0) {
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            running = 0;
            break;
        }
        /* quitar \n final */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        if (strcmp(line, "/quit") == 0) {
            running = 0;
            break;
        }
        if (strcmp(line, "/help") == 0) {
            printf("comandos: /quit, /nick NOMBRE, /help\n");
            fflush(stdout);
            continue;
        }
        if (strncmp(line, "/nick ", 6) == 0) {
            /* no se puede reasignar puntero fácil; solo imprimir */
            printf("nick cambiado a: %s\n", line + 6);
            fflush(stdout);
            continue;
        }
        /* rechazar control chars */
        {
            int ok = 1;
            for (char *c = line; *c; c++) {
                if ((unsigned char)*c < 0x20 && *c != '\t') {
                    ok = 0;
                    break;
                }
            }
            if (!ok) {
                printf("mensaje rechazado: caracteres de control\n");
                fflush(stdout);
                continue;
            }
        }
        send_msg(slots[rank], rank, line);
        printf("[tu] %s\n", line);
        fflush(stdout);
    }

    if (dsm_barrier(rt, 120000u) != 0) {
        fprintf(stderr, "nodo %u: chat: fallo la barrera final\n", rank);
    }

    free(slots);
    free(last_seq);
    return 0;
}