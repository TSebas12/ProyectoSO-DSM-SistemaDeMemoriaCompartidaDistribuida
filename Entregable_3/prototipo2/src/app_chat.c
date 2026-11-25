/*
 * app_chat.c — Chat de texto libre entre nodos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: Entrega final
 *
 * Cada nodo escribe en su propia página (una por nodo) de un arreglo DSM
 * compartido. Tras cada mensaje se llama dsm_release() para que el home
 * instale la copia y avise a los lectores; sin eso el dueño escribiría en
 * su copia M y nadie vería nada. Los demás nodos sondean las páginas y
 * muestran los mensajes nuevos.
 */
#include "dsm_apps.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
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
    chat_slot_t copy;
    uint32_t    s;

    s = atomic_load_explicit(&slot->seq, memory_order_acquire);
    if (s == 0u || s <= *last_seq) {
        return;
    }
    memcpy(&copy, slot, sizeof(copy));
    /* Revalidar el seq tras la copia: si cambió, la instantánea se descarta. */
    if (atomic_load_explicit(&slot->seq, memory_order_acquire) != s) {
        return;
    }
    if (copy.magic != DSM_CHAT_MAGIC) {
        return;
    }
    if (copy.len >= DSM_CHAT_TEXT) {
        return;
    }
    if (copy.checksum != fnv1a(copy.text, copy.len)) {
        return;
    }
    *last_seq = s;
    copy.text[copy.len] = '\0';
    printf("[nodo %" PRIu16 "] %s\n", copy.from, copy.text);
    fflush(stdout);
}

static void send_msg(dsm_runtime_t *rt, chat_slot_t *slot, uint32_t from,
                     const char *text)
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
    atomic_store_explicit(&slot->seq,
                          atomic_load_explicit(&slot->seq, memory_order_relaxed) + 1u,
                          memory_order_release);
    /*
     * Publicar el mensaje: sin dsm_release la copia queda en M y los lectores
     * nunca se enteran.
     */
    (void)dsm_release(rt, slot, sizeof(*slot));
}

int dsm_app_chat_run(dsm_runtime_t *rt, const dsm_app_opts_t *opts)
{
    uint32_t      nodes = dsm_node_count(rt);
    uint32_t      rank  = dsm_node_id(rt);
    uint32_t      page_size = dsm_page_size(rt);
    char         *buf;
    chat_slot_t **slots;
    uint32_t     *last_seq;
    char          nick[64];
    uint32_t      duration;
    uint32_t      start;
    int           running = 1;

    if (rt == NULL || opts == NULL) {
        return 1;
    }
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
    /* No se borra el arreglo completo: cada nodo inicializa solo su página. */
    memset(buf + (size_t)rank * page_size, 0, sizeof(chat_slot_t));

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

    if (opts->nick != NULL && opts->nick[0] != '\0') {
        snprintf(nick, sizeof(nick), "%s", opts->nick);
    } else {
        snprintf(nick, sizeof(nick), "nodo%u", rank);
    }

    duration = opts->duration;
    if (opts->text != NULL && opts->text[0] != '\0') {
        if (duration == 0u) {
            duration = 5u;
        }
    }

    if (dsm_barrier(rt, 120000u) != DSM_OK) {
        fprintf(stderr, "nodo %u: chat: fallo la barrera inicial\n", rank);
        free(slots);
        free(last_seq);
        return 1;
    }

    /* Publicar el texto inicial después de la barrera. */
    if (opts->text != NULL && opts->text[0] != '\0') {
        char msg[DSM_CHAT_TEXT];

        snprintf(msg, sizeof(msg), "%s", opts->text);
        send_msg(rt, slots[rank], rank, msg);
        printf("[tu] %s\n", msg);
        fflush(stdout);
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
        {
            int r = poll(&pfd, 1, DSM_CHAT_POLL_MS);

            if (r < 0) {
                if (errno == EINTR) {
                    break;
                }
                /* Error no recuperable: salir. */
                running = 0;
                break;
            }
            if (r == 0) {
                continue;
            }
            if (!(pfd.revents & POLLIN)) {
                /* POLLHUP/POLLERR/POLLNVAL: fin de entrada. */
                if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    running = 0;
                    break;
                }
                continue;
            }
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
            snprintf(nick, sizeof(nick), "%s", line + 6);
            printf("nick cambiado a: %s\n", nick);
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
        {
            char msg[DSM_CHAT_TEXT];

            snprintf(msg, sizeof(msg), "%s: %s", nick, line);
            send_msg(rt, slots[rank], rank, msg);
            printf("[tu] %s\n", msg);
            fflush(stdout);
        }
    }

    /* Barrera final best-effort: no bloquear si alguien salió antes. */
    (void)dsm_barrier(rt, 10000u);

    free(slots);
    free(last_seq);
    return 0;
}