#ifndef DSM_NET_H
#define DSM_NET_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "dsm_config.h"
#include "dsm_coherence.h"
#include "dsm_types.h"

typedef struct dsm_net dsm_net_t;

/* read exactly n bytes from fd */
int dsm_net_read_full(int fd, void *buf, size_t n);

dsm_net_t *dsm_net_create(const dsm_config_t *cfg, dsm_coherence_t *co);
void dsm_net_destroy(dsm_net_t *net);
dsm_status_t dsm_net_start(dsm_net_t *net);
void dsm_net_stop(dsm_net_t *net);

/* callbacks matching dsm_coherence_ops_t */
dsm_status_t dsm_net_send(void *ctx, const dsm_msg_t *msg);
dsm_status_t dsm_net_fetch_shared(void *ctx, uint64_t page_id, void *buf, size_t len, uint32_t timeout_ms);
dsm_status_t dsm_net_fetch_exclusive(void *ctx, uint64_t page_id, void *buf, size_t len, uint32_t timeout_ms);
dsm_status_t dsm_net_writeback(void *ctx, uint64_t page_id, const void *buf, size_t len);

dsm_coherence_ops_t dsm_net_ops(dsm_net_t *net);

#endif /* DSM_NET_H */