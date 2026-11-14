/*
 * dsm_config.h — Configuración del nodo DSM.
 */
#ifndef DSM_CONFIG_H
#define DSM_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_types.h"

typedef struct dsm_config {
    uint32_t config_version;
    uint32_t page_size;
    uint64_t node_memory;
    uint64_t window_size;
    uint32_t max_nodes;
    uint32_t node_id;
    char     bind_address[64];
    uint16_t listen_port;
    uint16_t control_port;
    uint16_t heartbeat_port;
    uint16_t web_port;
    uint16_t web_enabled;
    uint16_t reserved16;
    char     seed_host[256];
    uint16_t seed_port;
    uint16_t reserved16b;
    uint32_t heartbeat_interval_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t network_timeout_ms;
    uint32_t connect_timeout_ms;
    uint32_t retry_count;
    uint32_t retry_backoff_ms;
    uint32_t max_message_size;
    uint32_t thread_pool_size;
    uint32_t fault_queue_size;
    uint32_t eviction_policy;
    uint32_t log_level;
    char     log_file[256];
    uint32_t stats_interval_ms;
    uint32_t barrier_timeout_ms;
    uint32_t replication_factor;
    uint32_t checkpoint_interval_ms;
    uint32_t scheduler_policy;
    uint32_t port_range_start;
    uint32_t port_range_end;
    uint32_t reserved32;
} dsm_config_t;

_Static_assert(sizeof(dsm_config_t) == 704, "dsm_config_t debe medir 704 bytes");

dsm_status_t dsm_config_defaults(dsm_config_t *cfg);
dsm_status_t dsm_config_load(const char *path, dsm_config_t *cfg);
dsm_status_t dsm_config_validate(const dsm_config_t *cfg);
dsm_status_t dsm_config_save(const char *path, const dsm_config_t *cfg);

const char *dsm_log_level_name(uint32_t level);

#endif /* DSM_CONFIG_H */