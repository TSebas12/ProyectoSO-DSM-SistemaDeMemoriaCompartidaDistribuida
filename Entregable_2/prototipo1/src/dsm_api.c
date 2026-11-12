#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_api.h"
#include "dsm_runtime.h"

/* Objeto del E2: 4 páginas de 4 KB = 16 KB (ver documento del entregable). */
#define DSM_E2_OBJECT_PAGES 4u

dsm_status_t dsm_init(const dsm_config_t *cfg, dsm_runtime_t **out) {
    if (!cfg || !out) return DSM_ERR_INVAL;
    dsm_runtime_t *rt = calloc(1, sizeof(*rt));
    if (!rt) return DSM_ERR_NOMEM;
    dsm_status_t st = dsm_runtime_init(rt, cfg);
    if (st != DSM_OK) {
        /* Libera lo que se haya alcanzado a inicializar (memoria, red, etc.). */
        dsm_runtime_destroy(rt);
        free(rt);
        return st;
    }
    *out = rt;
    return DSM_OK;
}

void dsm_shutdown(dsm_runtime_t *rt) {
    if (!rt) return;
    dsm_runtime_destroy(rt);
    free(rt);
}

/*
 * Crea el objeto del E2 en el nodo home: cuatro páginas residentes y con
 * permiso de escritura. Los demás nodos no crean nada; su tabla de páginas se
 * llena con los fallos que resuelve el runtime.
 */
dsm_status_t dsm_create(dsm_runtime_t *rt) {
    uint32_t page_size;
    char *buf;
    dsm_status_t st = DSM_OK;
    uint32_t i;

    if (!rt) return DSM_ERR_INVAL;
    if (rt->cfg.node_id != rt->co.home_node) return DSM_OK;   /* solo el home */
    page_size = dsm_memory_page_size(&rt->mem);
    if (page_size == 0) return DSM_ERR_STATE;

    buf = malloc(page_size);
    if (!buf) return DSM_ERR_NOMEM;

    for (i = 0; i < DSM_E2_OBJECT_PAGES; i++) {
        dsm_page_entry_t *page = dsm_memory_page_get_or_create(&rt->mem, i, 0);
        if (!page) { st = DSM_ERR_NOMEM; break; }

        memset(buf, 0, page_size);
        snprintf(buf, page_size, "objeto DSM: pagina %u creada por el nodo %u",
                 i, rt->cfg.node_id);
        st = dsm_memory_copy_in(&rt->mem, i, buf, page_size);
        if (st != DSM_OK) break;

        if (dsm_mutex_lock(&page->lock) != DSM_OK) { st = DSM_ERR_STATE; break; }
        page->meta.residency = (uint8_t)DSM_RES_RESIDENT;
        page->meta.msi_state = (uint8_t)DSM_PAGE_M;
        page->meta.is_dirty  = 1;
        page->meta.version   = 1;
        if (page->meta.frame_slot == DSM_INVALID_FRAME) {
            page->meta.frame_slot = dsm_memory_alloc_frame(&rt->mem, i);
        }
        dsm_mutex_unlock(&page->lock);

        st = dsm_memory_protect(&rt->mem, i, DSM_PAGE_M);
        if (st != DSM_OK) break;
    }

    free(buf);
    return st;
}

/*
 * Registra el hilo actual en el manejador de SIGSEGV. Sin esto, el primer
 * fallo de página no tiene contexto y el proceso muere con la señal original.
 */
dsm_status_t dsm_attach(dsm_runtime_t *rt) {
    if (!rt) return DSM_ERR_INVAL;
    return dsm_fault_attach_thread(&rt->app_ctx);
}