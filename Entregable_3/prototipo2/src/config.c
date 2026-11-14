/*
 * config.c — Carga, validación y guardado de la configuración del nodo.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Formato del archivo: una pareja "clave = valor" por línea. Todo lo que sigue
 * a '#' es comentario. Las claves desconocidas se ignoran para que un archivo
 * de una versión posterior no rompa el arranque; los valores fuera de rango sí
 * se rechazan en dsm_config_validate().
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_config.h"

/* Quita espacios al inicio y al final, en el mismo buffer. */
static void trim(char *s)
{
    char  *p = s;
    size_t n;

    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1u);
    }
    n = strlen(s);
    while (n > 0u && isspace((unsigned char)s[n - 1u])) {
        s[--n] = '\0';
    }
}

/* Convierte un entero decimal sin signo; -1 si la cadena no es válida. */
static int parse_unsigned(const char *v, unsigned long *out)
{
    char          *end;
    unsigned long  value;

    errno = 0;
    value = strtoul(v, &end, 10);
    if (errno != 0 || *end != '\0' || end == v) {
        return -1;
    }
    *out = value;
    return 0;
}

/* Política de expulsión: "lru" (0) o "clock" (1); también acepta el número. */
static int parse_eviction(const char *v)
{
    unsigned long u;

    if (strcmp(v, "lru") == 0) {
        return 0;
    }
    if (strcmp(v, "clock") == 0) {
        return 1;
    }
    if (parse_unsigned(v, &u) == 0 && u <= 1u) {
        return (int)u;
    }
    return -1;
}

/* Política de planificación: "round_robin" (0) o "least_loaded" (1). */
static int parse_scheduler(const char *v)
{
    unsigned long u;

    if (strcmp(v, "round_robin") == 0) {
        return 0;
    }
    if (strcmp(v, "least_loaded") == 0) {
        return 1;
    }
    if (parse_unsigned(v, &u) == 0 && u <= 1u) {
        return (int)u;
    }
    return -1;
}

/* Nivel de log: error(0), warn(1), info(2), debug(3), trace(4). */
static int parse_log_level(const char *v)
{
    unsigned long u;

    if (strcmp(v, "error") == 0) {
        return 0;
    }
    if (strcmp(v, "warn") == 0) {
        return 1;
    }
    if (strcmp(v, "info") == 0) {
        return 2;
    }
    if (strcmp(v, "debug") == 0) {
        return 3;
    }
    if (strcmp(v, "trace") == 0) {
        return 4;
    }
    if (parse_unsigned(v, &u) == 0 && u <= 4u) {
        return (int)u;
    }
    return -1;
}

dsm_status_t dsm_config_defaults(dsm_config_t *cfg)
{
    if (cfg == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(cfg, 0, sizeof(*cfg));

    cfg->config_version        = 1;
    cfg->page_size             = 4096;
    cfg->node_memory           = 1048576;      /* 1 MB por nodo            */
    cfg->window_size           = 268435456;    /* ventana virtual de 256 MB */
    cfg->max_nodes             = 16;
    cfg->node_id               = 0;
    snprintf(cfg->bind_address, sizeof(cfg->bind_address), "0.0.0.0");
    cfg->listen_port           = 27300;
    cfg->control_port          = 27301;
    cfg->heartbeat_port        = 27302;
    cfg->web_port              = 27303;
    cfg->web_enabled           = 1;
    snprintf(cfg->seed_host, sizeof(cfg->seed_host), "127.0.0.1");
    cfg->seed_port             = 27300;
    cfg->heartbeat_interval_ms = 1000;
    cfg->heartbeat_timeout_ms  = 3000;
    cfg->network_timeout_ms    = 2000;
    cfg->connect_timeout_ms    = 2000;
    cfg->retry_count           = 3;
    cfg->retry_backoff_ms      = 250;
    cfg->max_message_size      = 8192;
    cfg->thread_pool_size      = 4;
    cfg->fault_queue_size      = 1024;
    cfg->eviction_policy       = 0;            /* LRU                      */
    cfg->log_level             = 2;            /* info                     */
    snprintf(cfg->log_file, sizeof(cfg->log_file), "logs/dsm.log");
    cfg->stats_interval_ms     = 5000;
    cfg->barrier_timeout_ms    = 30000;
    cfg->replication_factor    = 0;
    cfg->checkpoint_interval_ms = 0;
    cfg->scheduler_policy      = 0;            /* round_robin              */
    cfg->port_range_start      = 27300;
    cfg->port_range_end        = 27399;
    return DSM_OK;
}

dsm_status_t dsm_config_load(const char *path, dsm_config_t *cfg)
{
    FILE *f;
    char  line[1024];

    if (path == NULL || cfg == NULL) {
        return DSM_ERR_INVAL;
    }
    if (dsm_config_defaults(cfg) != DSM_OK) {
        return DSM_ERR_INVAL;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return DSM_ERR_NOENT;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char  *comment;
        char  *eq;
        char   key[128];
        char   val[512];
        size_t klen;
        unsigned long u;

        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';

        klen = (size_t)(eq - line);
        if (klen >= sizeof(key)) {
            klen = sizeof(key) - 1u;
        }
        memcpy(key, line, klen);
        key[klen] = '\0';
        snprintf(val, sizeof(val), "%s", eq + 1);
        trim(key);
        trim(val);
        if (key[0] == '\0') {
            continue;
        }

        if (strcmp(key, "config_version") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->config_version = (uint32_t)u;
        } else if (strcmp(key, "node_id") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->node_id = (uint32_t)u;
        } else if (strcmp(key, "bind_address") == 0) {
            snprintf(cfg->bind_address, sizeof(cfg->bind_address), "%.63s", val);
        } else if (strcmp(key, "listen_port") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->listen_port = (uint16_t)u;
        } else if (strcmp(key, "control_port") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->control_port = (uint16_t)u;
        } else if (strcmp(key, "heartbeat_port") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->heartbeat_port = (uint16_t)u;
        } else if (strcmp(key, "web_port") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->web_port = (uint16_t)u;
        } else if (strcmp(key, "web_enabled") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->web_enabled = (uint16_t)u;
        } else if (strcmp(key, "seed_host") == 0) {
            snprintf(cfg->seed_host, sizeof(cfg->seed_host), "%.255s", val);
        } else if (strcmp(key, "seed_port") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->seed_port = (uint16_t)u;
        } else if (strcmp(key, "page_size") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->page_size = (uint32_t)u;
        } else if (strcmp(key, "node_memory") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->node_memory = (uint64_t)u;
        } else if (strcmp(key, "window_size") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->window_size = (uint64_t)u;
        } else if (strcmp(key, "max_nodes") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->max_nodes = (uint32_t)u;
        } else if (strcmp(key, "heartbeat_interval_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->heartbeat_interval_ms = (uint32_t)u;
        } else if (strcmp(key, "heartbeat_timeout_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->heartbeat_timeout_ms = (uint32_t)u;
        } else if (strcmp(key, "network_timeout_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->network_timeout_ms = (uint32_t)u;
        } else if (strcmp(key, "connect_timeout_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->connect_timeout_ms = (uint32_t)u;
        } else if (strcmp(key, "retry_count") == 0 && parse_unsigned(val, &u) == 0) {
            cfg->retry_count = (uint32_t)u;
        } else if (strcmp(key, "retry_backoff_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->retry_backoff_ms = (uint32_t)u;
        } else if (strcmp(key, "max_message_size") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->max_message_size = (uint32_t)u;
        } else if (strcmp(key, "thread_pool_size") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->thread_pool_size = (uint32_t)u;
        } else if (strcmp(key, "fault_queue_size") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->fault_queue_size = (uint32_t)u;
        } else if (strcmp(key, "eviction_policy") == 0) {
            int e = parse_eviction(val);

            if (e >= 0) {
                cfg->eviction_policy = (uint32_t)e;
            }
        } else if (strcmp(key, "log_level") == 0) {
            int e = parse_log_level(val);

            if (e >= 0) {
                cfg->log_level = (uint32_t)e;
            }
        } else if (strcmp(key, "log_file") == 0) {
            snprintf(cfg->log_file, sizeof(cfg->log_file), "%.255s", val);
        } else if (strcmp(key, "stats_interval_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->stats_interval_ms = (uint32_t)u;
        } else if (strcmp(key, "barrier_timeout_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->barrier_timeout_ms = (uint32_t)u;
        } else if (strcmp(key, "replication_factor") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->replication_factor = (uint32_t)u;
        } else if (strcmp(key, "checkpoint_interval_ms") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->checkpoint_interval_ms = (uint32_t)u;
        } else if (strcmp(key, "scheduler_policy") == 0) {
            int e = parse_scheduler(val);

            if (e >= 0) {
                cfg->scheduler_policy = (uint32_t)e;
            }
        } else if (strcmp(key, "port_range_start") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->port_range_start = (uint32_t)u;
        } else if (strcmp(key, "port_range_end") == 0 &&
                   parse_unsigned(val, &u) == 0) {
            cfg->port_range_end = (uint32_t)u;
        }
    }

    fclose(f);
    return DSM_OK;
}

dsm_status_t dsm_config_validate(const dsm_config_t *cfg)
{
    uint16_t ports[4];
    int      i;
    int      j;

    if (cfg == NULL) {
        return DSM_ERR_INVAL;
    }
    if (cfg->config_version != 1u) {
        return DSM_ERR_NOTSUP;
    }
    if (cfg->node_id >= cfg->max_nodes) {
        return DSM_ERR_INVAL;
    }
    /* El tamaño de página debe ser potencia de dos y de al menos 4 KB. */
    if (cfg->page_size < 4096u || (cfg->page_size & (cfg->page_size - 1u)) != 0u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->node_memory % cfg->page_size != 0u) {
        return DSM_ERR_INVAL;
    }
    /* Menos de 16 marcos haría inviable cualquier conjunto de trabajo. */
    if (cfg->node_memory / cfg->page_size < 16u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->window_size < cfg->node_memory) {
        return DSM_ERR_INVAL;
    }
    if (cfg->window_size % cfg->page_size != 0u) {
        return DSM_ERR_INVAL;
    }

    /* Los cuatro puertos del nodo: DSM, control, heartbeat y web.          */
    ports[0] = cfg->listen_port;
    ports[1] = cfg->control_port;
    ports[2] = cfg->heartbeat_port;
    ports[3] = cfg->web_port;
    for (i = 0; i < 4; i++) {
        if (ports[i] < cfg->port_range_start || ports[i] > cfg->port_range_end) {
            return DSM_ERR_INVAL;
        }
    }
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 4; j++) {
            if (ports[i] == ports[j]) {
                return DSM_ERR_INVAL;
            }
        }
    }

    if (cfg->heartbeat_timeout_ms < 2u * cfg->heartbeat_interval_ms) {
        return DSM_ERR_INVAL;
    }
    if (cfg->max_message_size < cfg->page_size + 256u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->retry_count > 10u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->thread_pool_size < 1u || cfg->thread_pool_size > 64u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->log_level > 4u) {
        return DSM_ERR_INVAL;
    }
    if (cfg->replication_factor > cfg->max_nodes - 1u) {
        return DSM_ERR_INVAL;
    }
    return DSM_OK;
}

dsm_status_t dsm_config_save(const char *path, const dsm_config_t *cfg)
{
    FILE *f;

    if (path == NULL || cfg == NULL) {
        return DSM_ERR_INVAL;
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return DSM_ERR_PERM;
    }

    fprintf(f, "config_version = %u\n", cfg->config_version);
    fprintf(f, "node_id = %u\n", cfg->node_id);
    fprintf(f, "max_nodes = %u\n", cfg->max_nodes);
    fprintf(f, "bind_address = %s\n", cfg->bind_address);
    fprintf(f, "listen_port = %u\n", (unsigned)cfg->listen_port);
    fprintf(f, "control_port = %u\n", (unsigned)cfg->control_port);
    fprintf(f, "heartbeat_port = %u\n", (unsigned)cfg->heartbeat_port);
    fprintf(f, "web_port = %u\n", (unsigned)cfg->web_port);
    fprintf(f, "web_enabled = %u\n", (unsigned)cfg->web_enabled);
    fprintf(f, "seed_host = %s\n", cfg->seed_host);
    fprintf(f, "seed_port = %u\n", (unsigned)cfg->seed_port);
    fprintf(f, "page_size = %u\n", cfg->page_size);
    fprintf(f, "node_memory = %llu\n", (unsigned long long)cfg->node_memory);
    fprintf(f, "window_size = %llu\n", (unsigned long long)cfg->window_size);
    fprintf(f, "heartbeat_interval_ms = %u\n", cfg->heartbeat_interval_ms);
    fprintf(f, "heartbeat_timeout_ms = %u\n", cfg->heartbeat_timeout_ms);
    fprintf(f, "network_timeout_ms = %u\n", cfg->network_timeout_ms);
    fprintf(f, "connect_timeout_ms = %u\n", cfg->connect_timeout_ms);
    fprintf(f, "retry_count = %u\n", cfg->retry_count);
    fprintf(f, "retry_backoff_ms = %u\n", cfg->retry_backoff_ms);
    fprintf(f, "max_message_size = %u\n", cfg->max_message_size);
    fprintf(f, "thread_pool_size = %u\n", cfg->thread_pool_size);
    fprintf(f, "fault_queue_size = %u\n", cfg->fault_queue_size);
    fprintf(f, "eviction_policy = %u\n", cfg->eviction_policy);
    fprintf(f, "log_level = %u\n", cfg->log_level);
    fprintf(f, "log_file = %s\n", cfg->log_file);
    fprintf(f, "stats_interval_ms = %u\n", cfg->stats_interval_ms);
    fprintf(f, "barrier_timeout_ms = %u\n", cfg->barrier_timeout_ms);
    fprintf(f, "replication_factor = %u\n", cfg->replication_factor);
    fprintf(f, "checkpoint_interval_ms = %u\n", cfg->checkpoint_interval_ms);
    fprintf(f, "scheduler_policy = %u\n", cfg->scheduler_policy);
    fprintf(f, "port_range_start = %u\n", cfg->port_range_start);
    fprintf(f, "port_range_end = %u\n", cfg->port_range_end);

    fclose(f);
    return DSM_OK;
}

const char *dsm_log_level_name(uint32_t level)
{
    static const char *names[] = { "error", "warn", "info", "debug", "trace" };

    if (level > 4u) {
        return "unknown";
    }
    return names[level];
}
