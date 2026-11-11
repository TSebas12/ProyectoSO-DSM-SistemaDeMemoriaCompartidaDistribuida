#ifndef DSM_API_H
#define DSM_API_H

#include "dsm_config.h"
#include "dsm_types.h"

typedef struct dsm_runtime dsm_runtime_t;

dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out);
void dsm_shutdown(dsm_runtime_t *rt);
dsm_status_t dsm_create(dsm_runtime_t *rt);
dsm_status_t dsm_attach(dsm_runtime_t *rt);

#endif /* DSM_API_H */