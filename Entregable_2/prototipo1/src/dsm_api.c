#include <stdlib.h>

#include "dsm_api.h"
#include "dsm_runtime.h"

dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out) {
    if (!cfg || !out) return DSM_ERR_INVAL;
    dsm_runtime_t *rt = calloc(1, sizeof(*rt));
    if (!rt) return DSM_ERR_NOMEM;
    dsm_status_t st = dsm_runtime_init(rt, cfg);
    if (st != DSM_OK) { free(rt); return st; }
    *out = rt;
    return DSM_OK;
}

void dsm_shutdown(dsm_runtime_t *rt) {
    if (!rt) return;
    dsm_runtime_destroy(rt);
    free(rt);
}

dsm_status_t dsm_create(dsm_runtime_t *rt) { (void)rt; return DSM_OK; }
dsm_status_t dsm_attach(dsm_runtime_t *rt) { (void)rt; return DSM_OK; }