/*
 * memory.c — Ventana de direcciones, tabla de páginas y pool de marcos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * La ventana se reserva completa desde el inicio con PROT_NONE sobre un memfd
 * que se mapea una segunda vez en RW. Esa reserva no compromete memoria física:
 * el kernel solo crea la página cuando se escribe por primera vez. Habilitar una
 * página es entonces un mprotect, no un mmap.
 *
 * El doble mapeo es lo que permite al resolutor escribir los datos sin darles
 * permiso a los hilos de usuario: se copia por el respaldo y solo cuando el
 * contenido está completo se publica el permiso en la ventana.
 */
#include "dsm_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/* --- Utilidades internas ------------------------------------------------ */
static uint32_t bucket_of(const dsm_memory_t *mem, uint64_t page_id)
{
    return (uint32_t)(page_id % (uint64_t)mem->bucket_count);
}

static void free_page_table(dsm_memory_t *mem)
{
    uint32_t i;

    if (mem->buckets == NULL) {
        return;
    }
    for (i = 0; i < mem->bucket_count; i++) {
        dsm_page_entry_t *e = mem->buckets[i];
        while (e != NULL) {
            dsm_page_entry_t *next = e->next;
            dsm_cond_destroy(&e->cond);
            dsm_mutex_destroy(&e->lock);
            free(e);
            e = next;
        }
        mem->buckets[i] = NULL;
    }
    free(mem->buckets);
    mem->buckets = NULL;
}

/* --- Ciclo de vida ------------------------------------------------------ */
void dsm_memory_params_default(dsm_memory_params_t *p)
{
    if (p == NULL) {
        return;
    }
    p->window_size   = (size_t)256u * 1024u * 1024u;   /* 256 MB */
    p->page_size     = DSM_PAGE_SIZE_DEFAULT;
    p->frame_count   = 256;
    p->page_base_id  = 0;
    p->bucket_count  = DSM_MEMORY_BUCKETS_DEFAULT;
}

dsm_status_t dsm_memory_init(dsm_memory_t *mem, const dsm_memory_params_t *p)
{
    dsm_memory_params_t defaults;
    long sys_page;
    uint32_t i;

    if (mem == NULL) {
        return DSM_ERR_INVAL;
    }
    memset(mem, 0, sizeof(*mem));
    mem->backing_fd = -1;   /* 0 sería stdin: nunca se debe cerrar por error */

    if (p == NULL) {
        dsm_memory_params_default(&defaults);
        p = &defaults;
    }

    sys_page = sysconf(_SC_PAGESIZE);
    if (sys_page <= 0) {
        return DSM_ERR_INVAL;
    }

    mem->page_size   = (p->page_size == 0) ? DSM_PAGE_SIZE_DEFAULT : p->page_size;
    mem->page_base_id = p->page_base_id;
    mem->frame_count = (p->frame_count == 0) ? 256u : p->frame_count;
    mem->bucket_count = (p->bucket_count == 0) ? DSM_MEMORY_BUCKETS_DEFAULT
                                               : p->bucket_count;

    /* El tamaño de página debe ser múltiplo del que usa el kernel.        */
    if (mem->page_size < (uint32_t)sys_page ||
        (mem->page_size % (uint32_t)sys_page) != 0) {
        return DSM_ERR_INVAL;
    }

    mem->window_size = (p->window_size == 0)
                       ? (size_t)256u * 1024u * 1024u
                       : p->window_size;
    if ((mem->window_size % (size_t)mem->page_size) != 0) {
        return DSM_ERR_INVAL;
    }
    if ((mem->window_size / (size_t)mem->page_size) > (size_t)UINT32_MAX) {
        return DSM_ERR_OVERFLOW;
    }
    mem->page_count = (uint32_t)(mem->window_size / (size_t)mem->page_size);

    /*
     * Un solo objeto de memoria mapeado dos veces. La ventana se pide en
     * PROT_NONE y el respaldo en RW; como ambos son MAP_SHARED del mismo
     * memfd, comparten páginas físicas y el kernel las asigna al primer
     * acceso, no al reservar.
     */
    mem->backing_fd = memfd_create("dsm_window", MFD_CLOEXEC);
    if (mem->backing_fd < 0) {
        return DSM_ERR_MMAP;
    }
    if (ftruncate(mem->backing_fd, (off_t)mem->window_size) != 0) {
        close(mem->backing_fd);
        mem->backing_fd = -1;
        return DSM_ERR_MMAP;
    }

    mem->window_base = mmap(NULL, mem->window_size, PROT_NONE,
                            MAP_SHARED, mem->backing_fd, 0);
    if (mem->window_base == MAP_FAILED) {
        mem->window_base = NULL;
        close(mem->backing_fd);
        mem->backing_fd = -1;
        return DSM_ERR_MMAP;
    }

    mem->backing_base = mmap(NULL, mem->window_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, mem->backing_fd, 0);
    if (mem->backing_base == MAP_FAILED) {
        mem->backing_base = NULL;
        dsm_memory_destroy(mem);
        return DSM_ERR_MMAP;
    }

    mem->buckets = calloc(mem->bucket_count, sizeof(dsm_page_entry_t *));
    if (mem->buckets == NULL) {
        dsm_memory_destroy(mem);
        return DSM_ERR_NOMEM;
    }

    mem->frames = calloc(mem->frame_count, sizeof(dsm_frame_t));
    mem->free_stack = malloc((size_t)mem->frame_count * sizeof(uint32_t));
    if (mem->frames == NULL || mem->free_stack == NULL) {
        dsm_memory_destroy(mem);
        return DSM_ERR_NOMEM;
    }

    for (i = 0; i < mem->frame_count; i++) {
        mem->frames[i].slot = i;
        mem->frames[i].flags = DSM_FRAME_FREE;
        mem->frames[i].page_id = DSM_INVALID_PAGE_ID;
        mem->frames[i].addr = NULL;
        mem->free_stack[i] = i;
    }
    mem->free_top = mem->frame_count;

    if (dsm_mutex_init(&mem->table_lock, DSM_LOCK_GLOBAL, "tabla_paginas") != DSM_OK) {
        dsm_memory_destroy(mem);
        return DSM_ERR_NOMEM;
    }
    if (dsm_mutex_init(&mem->frame_lock, DSM_LOCK_FRAME, "marcos") != DSM_OK) {
        dsm_memory_destroy(mem);
        return DSM_ERR_NOMEM;
    }

    mem->initialized = 1;
    return DSM_OK;
}

void dsm_memory_destroy(dsm_memory_t *mem)
{
    if (mem == NULL) {
        return;
    }
    free_page_table(mem);

    if (mem->frames != NULL) {
        free(mem->frames);
        mem->frames = NULL;
    }
    if (mem->free_stack != NULL) {
        free(mem->free_stack);
        mem->free_stack = NULL;
    }
    if (mem->window_base != NULL) {
        munmap(mem->window_base, mem->window_size);
        mem->window_base = NULL;
    }
    if (mem->backing_base != NULL) {
        munmap(mem->backing_base, mem->window_size);
        mem->backing_base = NULL;
    }
    if (mem->backing_fd >= 0) {
        close(mem->backing_fd);
        mem->backing_fd = -1;
    }
    if (mem->table_lock.initialized) {
        dsm_mutex_destroy(&mem->table_lock);
    }
    if (mem->frame_lock.initialized) {
        dsm_mutex_destroy(&mem->frame_lock);
    }
    mem->initialized = 0;
}

/* --- Consulta de la ventana --------------------------------------------- */
void *dsm_memory_window_base(const dsm_memory_t *mem)
{
    return (mem == NULL) ? NULL : mem->window_base;
}

size_t dsm_memory_window_size(const dsm_memory_t *mem)
{
    return (mem == NULL) ? 0 : mem->window_size;
}

uint32_t dsm_memory_page_size(const dsm_memory_t *mem)
{
    return (mem == NULL) ? 0 : mem->page_size;
}

uint32_t dsm_memory_page_count(const dsm_memory_t *mem)
{
    return (mem == NULL) ? 0 : mem->page_count;
}

uint64_t dsm_memory_page_base_id(const dsm_memory_t *mem)
{
    return (mem == NULL) ? DSM_INVALID_PAGE_ID : mem->page_base_id;
}

uint64_t dsm_memory_addr_to_page(const dsm_memory_t *mem, const void *addr)
{
    uintptr_t a;
    uintptr_t base;
    uint64_t index;

    if (mem == NULL || mem->window_base == NULL || addr == NULL) {
        return DSM_INVALID_PAGE_ID;
    }
    a = (uintptr_t)addr;
    base = (uintptr_t)mem->window_base;

    if (a < base || a >= base + (uintptr_t)mem->window_size) {
        return DSM_INVALID_PAGE_ID;
    }
    index = (uint64_t)((a - base) / (uintptr_t)mem->page_size);
    return mem->page_base_id + index;
}

void *dsm_memory_page_addr(const dsm_memory_t *mem, uint64_t page_id)
{
    uint64_t index;

    if (mem == NULL || mem->window_base == NULL) {
        return NULL;
    }
    if (page_id == DSM_INVALID_PAGE_ID || page_id < mem->page_base_id) {
        return NULL;
    }
    index = page_id - mem->page_base_id;
    if (index >= (uint64_t)mem->page_count) {
        return NULL;
    }
    return (uint8_t *)mem->window_base + (size_t)index * (size_t)mem->page_size;
}

/* --- Tabla de páginas --------------------------------------------------- */
dsm_page_entry_t *dsm_memory_page_lookup(dsm_memory_t *mem, uint64_t page_id)
{
    dsm_page_entry_t *e;
    uint32_t b;

    if (mem == NULL || !mem->initialized) {
        return NULL;
    }
    b = bucket_of(mem, page_id);
    if (dsm_mutex_lock(&mem->table_lock) != DSM_OK) {
        return NULL;
    }
    for (e = mem->buckets[b]; e != NULL; e = e->next) {
        if (e->meta.page_id == page_id) {
            break;
        }
    }
    dsm_mutex_unlock(&mem->table_lock);
    return e;
}

dsm_page_entry_t *dsm_memory_page_get_or_create(dsm_memory_t *mem, uint64_t page_id,
                                                uint64_t object_id)
{
    dsm_page_entry_t *e;
    dsm_page_entry_t *nuevo;
    uint32_t b;
    void *addr;

    if (mem == NULL || !mem->initialized) {
        return NULL;
    }
    addr = dsm_memory_page_addr(mem, page_id);
    if (addr == NULL) {
        return NULL;
    }
    b = bucket_of(mem, page_id);

    if (dsm_mutex_lock(&mem->table_lock) != DSM_OK) {
        return NULL;
    }
    for (e = mem->buckets[b]; e != NULL; e = e->next) {
        if (e->meta.page_id == page_id) {
            dsm_mutex_unlock(&mem->table_lock);
            return e;
        }
    }

    /*
     * Se crea fuera del lock la parte que puede fallar (calloc) para no
     * mantener el lock global mientras el sistema reserva memoria.
     */
    dsm_mutex_unlock(&mem->table_lock);

    nuevo = calloc(1, sizeof(*nuevo));
    if (nuevo == NULL) {
        return NULL;
    }
    nuevo->meta.page_id      = page_id;
    nuevo->meta.object_id    = object_id;
    nuevo->meta.version      = 0;
    nuevo->meta.local_addr   = addr;
    nuevo->meta.frame_slot   = DSM_INVALID_FRAME;
    nuevo->meta.refcount     = 0;
    nuevo->meta.msi_state    = (uint8_t)DSM_PAGE_I;
    nuevo->meta.residency    = (uint8_t)DSM_RES_NOT_RESIDENT;
    nuevo->meta.is_dirty     = 0;
    nuevo->meta.flags        = 0;

    if (dsm_mutex_init(&nuevo->lock, DSM_LOCK_PAGE, "pagina") != DSM_OK ||
        dsm_cond_init(&nuevo->cond) != DSM_OK) {
        dsm_mutex_destroy(&nuevo->lock);
        free(nuevo);
        return NULL;
    }

    /* Reintento de inserción: otro hilo pudo crearla mientras tanto.       */
    if (dsm_mutex_lock(&mem->table_lock) != DSM_OK) {
        dsm_cond_destroy(&nuevo->cond);
        dsm_mutex_destroy(&nuevo->lock);
        free(nuevo);
        return NULL;
    }
    for (e = mem->buckets[b]; e != NULL; e = e->next) {
        if (e->meta.page_id == page_id) {
            dsm_mutex_unlock(&mem->table_lock);
            dsm_cond_destroy(&nuevo->cond);
            dsm_mutex_destroy(&nuevo->lock);
            free(nuevo);
            return e;
        }
    }
    nuevo->next = mem->buckets[b];
    mem->buckets[b] = nuevo;
    dsm_mutex_unlock(&mem->table_lock);

    return nuevo;
}

/* --- Permisos ----------------------------------------------------------- */
dsm_status_t dsm_memory_protect(dsm_memory_t *mem, uint64_t page_id, dsm_msi_state_t state)
{
    void *addr;
    int prot;

    addr = dsm_memory_page_addr(mem, page_id);
    if (addr == NULL) {
        return DSM_ERR_NOT_MAPPED;
    }

    switch (state) {
    case DSM_PAGE_S:
        prot = PROT_READ;
        break;
    case DSM_PAGE_M:
        prot = PROT_READ | PROT_WRITE;
        break;
    case DSM_PAGE_I:
    default:
        prot = PROT_NONE;
        break;
    }

    if (mprotect(addr, (size_t)mem->page_size, prot) != 0) {
        return DSM_ERR_MPROTECT;
    }
    return DSM_OK;
}

/* --- Copia de datos ----------------------------------------------------- */
/*
 * Dirección de la página dentro del mapeo de respaldo. Es la vista siempre
 * escribible de la misma página física; nunca se entrega al usuario.
 */
static void *backing_page_addr(const dsm_memory_t *mem, uint64_t page_id)
{
    uint64_t index;

    if (mem == NULL || mem->backing_base == NULL) {
        return NULL;
    }
    if (page_id == DSM_INVALID_PAGE_ID || page_id < mem->page_base_id) {
        return NULL;
    }
    index = page_id - mem->page_base_id;
    if (index >= (uint64_t)mem->page_count) {
        return NULL;
    }
    return (uint8_t *)mem->backing_base + (size_t)index * (size_t)mem->page_size;
}

dsm_status_t dsm_memory_copy_in(dsm_memory_t *mem, uint64_t page_id,
                                const void *src, size_t len)
{
    void *addr;

    if (src == NULL) {
        return DSM_ERR_INVAL;
    }
    if (len > (size_t)mem->page_size) {
        return DSM_ERR_OVERFLOW;
    }
    /*
     * Se escribe por el respaldo, no por la ventana: la ventana sigue en
     * PROT_NONE y ningún hilo de usuario puede ver los datos a medio copiar.
     * Los permisos se publican después con dsm_memory_protect().
     */
    addr = backing_page_addr(mem, page_id);
    if (addr == NULL) {
        return DSM_ERR_NOT_MAPPED;
    }
    memcpy(addr, src, len);
    return DSM_OK;
}

dsm_status_t dsm_memory_copy_out(dsm_memory_t *mem, uint64_t page_id,
                                 void *dst, size_t len)
{
    void *addr;

    if (dst == NULL) {
        return DSM_ERR_INVAL;
    }
    if (len > (size_t)mem->page_size) {
        return DSM_ERR_OVERFLOW;
    }
    addr = backing_page_addr(mem, page_id);
    if (addr == NULL) {
        return DSM_ERR_NOT_MAPPED;
    }
    memcpy(dst, addr, len);
    return DSM_OK;
}

/* --- Marcos ------------------------------------------------------------- */
uint32_t dsm_memory_alloc_frame(dsm_memory_t *mem, uint64_t page_id)
{
    uint32_t slot;
    void *addr;

    if (mem == NULL || !mem->initialized) {
        return DSM_INVALID_FRAME;
    }
    addr = dsm_memory_page_addr(mem, page_id);
    if (addr == NULL) {
        return DSM_INVALID_FRAME;
    }
    if (dsm_mutex_lock(&mem->frame_lock) != DSM_OK) {
        return DSM_INVALID_FRAME;
    }
    if (mem->free_top == 0) {
        dsm_mutex_unlock(&mem->frame_lock);
        return DSM_INVALID_FRAME;
    }
    slot = mem->free_stack[--mem->free_top];
    mem->frames[slot].flags = DSM_FRAME_USED;
    mem->frames[slot].page_id = page_id;
    mem->frames[slot].addr = addr;
    mem->frames[slot].last_used_ms = dsm_now_ms();
    mem->frames[slot].state = (uint32_t)DSM_RES_RESIDENT;
    dsm_mutex_unlock(&mem->frame_lock);
    return slot;
}

void dsm_memory_release_frame(dsm_memory_t *mem, uint32_t slot)
{
    if (mem == NULL || !mem->initialized || slot >= mem->frame_count) {
        return;
    }
    if (dsm_mutex_lock(&mem->frame_lock) != DSM_OK) {
        return;
    }
    if (mem->frames[slot].flags == DSM_FRAME_USED) {
        mem->frames[slot].flags = DSM_FRAME_FREE;
        mem->frames[slot].page_id = DSM_INVALID_PAGE_ID;
        mem->frames[slot].addr = NULL;
        if (mem->free_top < mem->frame_count) {
            mem->free_stack[mem->free_top++] = slot;
        }
    }
    dsm_mutex_unlock(&mem->frame_lock);
}

void *dsm_memory_frame_addr(const dsm_memory_t *mem, uint32_t slot)
{
    if (mem == NULL || slot >= mem->frame_count) {
        return NULL;
    }
    return mem->frames[slot].addr;
}

void dsm_memory_usage(dsm_memory_t *mem, uint64_t *resident, uint64_t *free_frames)
{
    uint32_t libres = 0;

    if (mem == NULL || !mem->initialized) {
        if (resident != NULL)   { *resident = 0; }
        if (free_frames != NULL) { *free_frames = 0; }
        return;
    }
    if (dsm_mutex_lock(&mem->frame_lock) == DSM_OK) {
        libres = mem->free_top;
        dsm_mutex_unlock(&mem->frame_lock);
    }
    if (free_frames != NULL) {
        *free_frames = (uint64_t)libres;
    }
    if (resident != NULL) {
        *resident = (uint64_t)(mem->frame_count - libres);
    }
}
