/*
 * dsm_memory.h — Ventana de direcciones, tabla de páginas, marcos y swap.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * La ventana es una reserva de direcciones con mmap(PROT_NONE). Cada página se
 * habilita con mprotect cuando el resolutor la trae. La tabla de páginas es un
 * hash con encadenamiento: solo existen entradas para las páginas que se
 * tocaron, no para las 65536 que caben en la ventana.
 *
 * Novedades del prototipo 2:
 *   - pool de marcos con política LRU aproximada (last_used_ms),
 *   - almacén de intercambio local (swap) para no perder páginas al expulsar.
 * El swap es un archivo disperso del tamaño de la ventana; solo se escriben las
 * páginas que realmente se expulsan.
 */
#ifndef DSM_MEMORY_H
#define DSM_MEMORY_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "dsm_sync.h"
#include "dsm_types.h"

#define DSM_MEMORY_BUCKETS_DEFAULT 1024u
#define DSM_SWAP_PATH_MAX          256u

/*
 * Metadatos de una página local. Se conserva el tamaño de 56 bytes del E1
 * (§11.7); los objetos de sincronización viven aparte, en dsm_page_entry_t,
 * para no romper el contrato de tamaño documentado.
 */
typedef struct dsm_local_page {
    uint64_t page_id;                 /*  0 */
    uint64_t object_id;               /*  8 */
    uint64_t version;                 /* 16 */
    void    *local_addr;              /* 24 */
    uint32_t frame_slot;              /* 32 */
    uint32_t refcount;                /* 36 */
    uint8_t  msi_state;               /* 40: DSM_PAGE_I / S / M */
    uint8_t  residency;               /* 41: DSM_RES_* */
    uint8_t  is_dirty;                /* 42 */
    uint8_t  flags;                   /* 43 */
    uint32_t pending_request_id_low;  /* 44 */
    uint32_t reserved;                /* 48 */
    uint32_t reserved2;               /* 52 */
} dsm_local_page_t;                   /* 56 bytes */

_Static_assert(sizeof(dsm_local_page_t) == 56, "dsm_local_page_t debe medir 56 bytes");

/* Entrada de la tabla: metadatos + sincronización + cadena del hash.      */
typedef struct dsm_page_entry {
    dsm_local_page_t       meta;
    dsm_mutex_t            lock;   /* Protege meta y la transición MSI      */
    dsm_cond_t             cond;   /* Despierta a los hilos que esperan     */
    struct dsm_page_entry *next;
} dsm_page_entry_t;

/* Marco del pool. El almacenamiento real es la página de la ventana.      */
typedef struct dsm_frame {
    uint32_t slot;           /*  0 */
    uint32_t flags;          /*  4: DSM_FRAME_FREE / USED */
    uint64_t page_id;        /*  8 */
    void    *addr;           /* 16 */
    uint64_t last_used_ms;   /* 24 */
    uint32_t owner_node;     /* 32 */
    uint32_t state;          /* 36 */
} dsm_frame_t;               /* 40 bytes */

_Static_assert(sizeof(dsm_frame_t) == 40, "dsm_frame_t debe medir 40 bytes");

#define DSM_FRAME_FREE  0u
#define DSM_FRAME_USED  1u

/*
 * Banderas de dsm_local_page_t.flags.
 * SWAP: la página se expulsó al archivo de intercambio y sigue siendo la
 * única copia; al volver a tocarla se restaura con dsm_memory_swap_in().
 */
#define DSM_PAGE_FLAG_SWAP  0x01u

/* Parámetros de creación. Se pasan explícitos para no depender del parser. */
typedef struct dsm_memory_params {
    size_t   window_size;    /* Bytes reservados; múltiplo de page_size       */
    uint32_t page_size;      /* 4096 por defecto                              */
    uint32_t frame_count;    /* Máximo de páginas residentes a la vez         */
    uint64_t page_base_id;   /* page_id de la primera página de la ventana    */
    uint32_t bucket_count;   /* Cubetas del hash; 0 usa el valor por defecto  */
} dsm_memory_params_t;

typedef struct dsm_memory {
    void             *window_base;
    size_t            window_size;
    uint32_t          page_size;
    uint32_t          page_count;
    uint64_t          page_base_id;

    dsm_page_entry_t **buckets;
    uint32_t           bucket_count;
    dsm_mutex_t        table_lock;

    dsm_frame_t       *frames;
    uint32_t           frame_count;
    uint32_t          *free_stack;
    uint32_t           free_top;
    dsm_mutex_t        frame_lock;

    /*
     * Segundo mapeo de las mismas páginas físicas, siempre en RW.
     *
     * La ventana y el respaldo son dos vistas del mismo objeto de memoria: la
     * ventana controla los permisos que ven los hilos de usuario y el respaldo
     * permite al resolutor escribir los datos sin conceder permisos a nadie.
     * Sin este doble mapeo habría que poner la ventana en RW antes de copiar y
     * otro hilo podría leer la página a medio escribir.
     */
    void              *backing_base;
    int                backing_fd;

    /* --- Almacén de intercambio (nuevo en el prototipo 2) --------------- */
    int                swap_fd;
    char               swap_path[DSM_SWAP_PATH_MAX];
    _Atomic uint64_t   swap_bytes;

    int                initialized;
} dsm_memory_t;

/* --- Ciclo de vida ------------------------------------------------------ */
void        dsm_memory_params_default(dsm_memory_params_t *p);
dsm_status_t dsm_memory_init(dsm_memory_t *mem, const dsm_memory_params_t *p);
void        dsm_memory_destroy(dsm_memory_t *mem);

/* --- Consulta de la ventana --------------------------------------------- */
void    *dsm_memory_window_base(const dsm_memory_t *mem);
size_t   dsm_memory_window_size(const dsm_memory_t *mem);
uint32_t dsm_memory_page_size(const dsm_memory_t *mem);
uint32_t dsm_memory_page_count(const dsm_memory_t *mem);
uint64_t dsm_memory_page_base_id(const dsm_memory_t *mem);

/* Traduce una dirección de la ventana a page_id, o DSM_INVALID_PAGE_ID.   */
uint64_t dsm_memory_addr_to_page(const dsm_memory_t *mem, const void *addr);
/* Dirección de la página dentro de la ventana, o NULL si está fuera.      */
void    *dsm_memory_page_addr(const dsm_memory_t *mem, uint64_t page_id);

/* --- Tabla de páginas --------------------------------------------------- */
dsm_page_entry_t *dsm_memory_page_lookup(dsm_memory_t *mem, uint64_t page_id);
dsm_page_entry_t *dsm_memory_page_get_or_create(dsm_memory_t *mem, uint64_t page_id,
                                                uint64_t object_id);

/* --- Permisos ----------------------------------------------------------- */
/*
 * Aplica los permisos que corresponden al estado MSI:
 *   I -> PROT_NONE, S -> PROT_READ, M -> PROT_READ|PROT_WRITE.
 * No modifica los metadatos: eso lo hace la capa de coherencia bajo el lock
 * de la página.
 */
dsm_status_t dsm_memory_protect(dsm_memory_t *mem, uint64_t page_id, dsm_msi_state_t state);

/* --- Copia de datos ----------------------------------------------------- */
/*
 * Copia datos hacia la página a través del mapeo de respaldo, sin tocar los
 * permisos de la ventana. El llamador publica después el estado con
 * dsm_memory_protect(), y solo entonces los hilos de usuario ven los datos.
 * Esa separación es la que evita que alguien lea la página a medio copiar.
 */
dsm_status_t dsm_memory_copy_in(dsm_memory_t *mem, uint64_t page_id,
                                const void *src, size_t len);
/* Copia datos desde la página hacia un buffer externo, también por el
 * respaldo: no altera los permisos de la ventana. */
dsm_status_t dsm_memory_copy_out(dsm_memory_t *mem, uint64_t page_id,
                                 void *dst, size_t len);
/* Rellena una página completa con un byte (para objetos nuevos).          */
dsm_status_t dsm_memory_fill(dsm_memory_t *mem, uint64_t page_id, uint8_t value);

/* --- Marcos ------------------------------------------------------------- */
uint32_t dsm_memory_alloc_frame(dsm_memory_t *mem, uint64_t page_id);
void     dsm_memory_release_frame(dsm_memory_t *mem, uint32_t slot);
void    *dsm_memory_frame_addr(const dsm_memory_t *mem, uint32_t slot);
/* Devuelve páginas residentes y marcos libres en este instante.           */
void     dsm_memory_usage(dsm_memory_t *mem, uint64_t *resident, uint64_t *free_frames);
/* Marca la página como usada ahora mismo (aproximación de LRU).           */
void     dsm_memory_touch(dsm_memory_t *mem, uint64_t page_id);
/*
 * Elige la página residente menos usada que lleve al menos min_age_ms sin
 * tocarse. avoid_page se excluye (la página que se está instalando no puede
 * ser su propia víctima). Devuelve DSM_INVALID_PAGE_ID si no hay candidata.
 */
uint64_t dsm_memory_pick_victim(dsm_memory_t *mem, uint32_t min_age_ms,
                                uint64_t avoid_page);

/* --- Swap --------------------------------------------------------------- */
/* Abre (o crea) el archivo de intercambio. Si path es NULL, no hay swap.  */
dsm_status_t dsm_memory_swap_open(dsm_memory_t *mem, const char *path);
void         dsm_memory_swap_close(dsm_memory_t *mem);
int          dsm_memory_swap_enabled(const dsm_memory_t *mem);
/* Escribe la página al swap (swap_out) o la restaura (swap_in).           */
dsm_status_t dsm_memory_swap_out(dsm_memory_t *mem, uint64_t page_id);
dsm_status_t dsm_memory_swap_in(dsm_memory_t *mem, uint64_t page_id);

#endif /* DSM_MEMORY_H */
