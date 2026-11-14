/*
 * dsm_coherence.h — Protocolo MSI, directorio y resolución de fallos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * La coherencia no habla sockets: recibe y entrega dsm_msg_t y delega el
 * transporte en dsm_coherence_ops_t. Respecto del prototipo 1 el contrato se
 * redujo a dos operaciones:
 *
 *   send()  envío sin respuesta (notificaciones, release, ACK).
 *   call()  RPC bloqueante con tiempo límite; la usa el home para invalidar a
 *           un tercero o para pedirle la página al dueño actual.
 *
 * El prototipo 1 tenía cuatro callbacks específicos (fetch_shared,
 * fetch_exclusive, writeback...). Al llegar los escenarios de tres nodos hubo
 * que añadir invalidación y reenvío; en lugar de seguir ampliando la lista se
 * dejó una sola puerta genérica y la coherencia arma el mensaje.
 */
#ifndef DSM_COHERENCE_H
#define DSM_COHERENCE_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_fault.h"
#include "dsm_memory.h"
#include "dsm_protocol.h"
#include "dsm_stats.h"
#include "dsm_types.h"

/* Entrada del directorio del nodo home. Tamaño del E1 (§11.4).           */
typedef struct dsm_directory_entry {
    uint64_t page_id;            /*  0 */
    uint64_t object_id;          /*  8 */
    uint64_t version;            /* 16 */
    uint64_t sharers;            /* 24: bitmap de nodos */
    uint64_t last_update_ms;     /* 32 */
    uint32_t owner_node;         /* 40 */
    uint32_t home_node;          /* 44 */
    uint64_t pending_request_id; /* 48 */
    uint8_t  state;              /* 56 */
    uint8_t  flags;              /* 57 */
    uint16_t reserved;           /* 58 */
} dsm_directory_entry_t;         /* 64 bytes */

_Static_assert(sizeof(dsm_directory_entry_t) == 64,
               "dsm_directory_entry_t debe medir 64 bytes");

/*
 * Operaciones que debe proveer la capa de red.
 *
 * send()  envía un mensaje sin esperar respuesta. No conserva payload/data
 *         después de retornar.
 * call()  RPC bloqueante. Deja la respuesta en resp (que puede traer data
 *         asignada con malloc; el llamador la libera) y devuelve DSM_OK solo
 *         si hubo respuesta válida.
 */
typedef struct dsm_coherence_ops {
    void         *ctx;
    dsm_status_t (*send)(void *ctx, const dsm_msg_t *msg);
    dsm_status_t (*call)(void *ctx, const dsm_msg_t *req, dsm_msg_t *resp,
                         uint32_t timeout_ms);
} dsm_coherence_ops_t;

typedef struct dsm_dir_entry {
    dsm_directory_entry_t  meta;
    /*
     * Serializa las operaciones de esta página en el nodo home. Se toma antes
     * de hablar con la red y se suelta al terminar; el lock de página solo se
     * toma para leer o escribir metadatos, nunca durante un RPC.
     */
    dsm_mutex_t            op_lock;
    struct dsm_dir_entry  *next;
} dsm_dir_entry_t;

/* Objeto del espacio compartido: un rango contiguo de páginas globales.   */
#define DSM_MAX_OBJECTS 128u

typedef struct dsm_object {
    uint64_t object_id;
    uint64_t page_base;
    uint64_t size;
    uint32_t page_count;
    uint32_t owner_node;   /* Nodo que pidió la creación                 */
    uint32_t tag;          /* Etiqueta legible para dsmctl               */
    uint32_t flags;
} dsm_object_t;

typedef struct dsm_coherence_params {
    uint32_t node_id;      /* Nodo local                                  */
    uint32_t home_node;    /* Nodo que guarda el directorio (0)           */
    uint32_t n_nodes;      /* Nodos del clúster                           */
    uint32_t timeout_ms;   /* Espera de los RPC de página; 0 = 3000       */
    uint32_t dir_buckets;  /* Cubetas del directorio; 0 = 256             */
} dsm_coherence_params_t;

typedef struct dsm_coherence {
    dsm_memory_t       *mem;
    dsm_stats_t        *stats;
    uint32_t            node_id;
    uint32_t            home_node;
    uint32_t            n_nodes;
    uint32_t            timeout_ms;
    dsm_coherence_ops_t ops;

    dsm_dir_entry_t   **dir;
    uint32_t            dir_buckets;
    dsm_mutex_t         dir_lock;

    /* --- Objetos del espacio compartido -------------------------------- */
    dsm_object_t        objects[DSM_MAX_OBJECTS];
    uint32_t            object_count;
    uint64_t            next_page;     /* cursor global de asignación     */
    uint64_t            object_seq;    /* último object_id repartido      */
    dsm_mutex_t         obj_lock;

    /*
     * Asignación determinista por secuencia. Cada nodo numera sus llamadas a
     * dsm_malloc (alloc_seq); el home guarda el rango que entregó para cada
     * secuencia y lo repite a los demás nodos. Así todos obtienen el mismo
     * page_base y el mismo object_id, que es el contrato SPMD del API.
     * La tabla también absorbe los reintentos de red.
     */
    struct {
        dsm_alloc_reply_t reply;
        uint8_t           valid;
    } alloc_cache[DSM_MAX_OBJECTS];
    uint64_t            alloc_seq;
    _Atomic uint64_t    next_request_id;

    /* --- Barrera centralizada ------------------------------------------ */
    /* La ronda se lee desde hilos que no tienen el lock, así que es atómica. */
    _Atomic uint32_t    barrier_round;
    _Atomic uint32_t    barrier_arrived;
    /* Última ronda en la que se registró cada nodo: evita contar dos veces
     * los sondeos repetidos de un mismo nodo. */
    uint32_t            barrier_registered_round[DSM_MAX_NODES];
    dsm_mutex_t         barrier_lock;

    int                 initialized;
} dsm_coherence_t;

/* Vista de solo lectura de una página, para dsmctl y para los logs.      */
typedef struct dsm_page_snapshot {
    uint64_t page_id;
    uint64_t version;
    uint32_t frame_slot;
    uint32_t home_node;
    uint32_t owner_node;
    uint64_t sharers;
    uint8_t  msi_state;
    uint8_t  residency;
    uint8_t  is_dirty;
} dsm_page_snapshot_t;

/* --- Ciclo de vida ------------------------------------------------------ */
dsm_status_t dsm_coherence_init(dsm_coherence_t *co, dsm_memory_t *mem,
                                dsm_stats_t *stats,
                                const dsm_coherence_params_t *p,
                                const dsm_coherence_ops_t *ops);
void         dsm_coherence_destroy(dsm_coherence_t *co);

/* --- Transiciones ------------------------------------------------------- */
/* Función pura: devuelve el estado siguiente. Se prueba sin red ni hilos.  */
dsm_msi_state_t dsm_msi_next_state(dsm_msi_state_t current, dsm_msi_event_t ev);

/* --- Fallos de página --------------------------------------------------- */
/* Resuelve un fallo: trae la página, publica permisos y actualiza el MSI.  */
dsm_status_t dsm_coherence_handle_fault(dsm_coherence_t *co, const dsm_fault_req_t *req);

/* --- Mensajes entrantes (lado servidor) --------------------------------- */
/*
 * Se llama con los mensajes que el nodo debe atender como servidor:
 * PAGE_REQUEST, OWNERSHIP_REQUEST, PAGE_FORWARD, PAGE_RELEASE, INVALIDATE,
 * PAGE_WRITEBACK, OBJECT_ALLOC y BARRIER. Las respuestas de un RPC las
 * consume la capa de red y no llegan aquí.
 */
dsm_status_t dsm_coherence_handle_message(dsm_coherence_t *co, const dsm_msg_t *msg);

/* --- Consulta ----------------------------------------------------------- */
dsm_status_t dsm_coherence_snapshot(dsm_coherence_t *co, uint64_t page_id,
                                    dsm_page_snapshot_t *out);

/* --- Objetos ------------------------------------------------------------ */
/*
 * Reserva un rango de páginas. En el home asigna localmente; en un cliente
 * hace un RPC al home. Todos los nodos deben pedir los objetos en el mismo
 * orden para obtener el mismo page_base (contrato SPMD del API).
 */
dsm_status_t dsm_coherence_alloc(dsm_coherence_t *co, uint64_t size, uint32_t tag,
                                 dsm_alloc_reply_t *out);
/* Busca un objeto ya asignado (por id o por tag).                        */
dsm_status_t dsm_coherence_find_object(dsm_coherence_t *co, uint64_t object_id,
                                       uint32_t tag, dsm_object_t *out);

/* --- Barrera ------------------------------------------------------------ */
/* Barrera centralizada por sondeo; no bloquea hilos del home.            */
dsm_status_t dsm_coherence_barrier(dsm_coherence_t *co, uint32_t timeout_ms);

/* --- Volcados de texto para dsmctl ------------------------------------- */
int dsm_coherence_format_pages(dsm_coherence_t *co, char *buf, size_t len);
int dsm_coherence_format_dir(dsm_coherence_t *co, char *buf, size_t len);
int dsm_coherence_format_objects(dsm_coherence_t *co, char *buf, size_t len);

/*
 * Fuerza la expulsión de hasta `count` páginas residentes. La usa la
 * aplicación `evict` para provocar presión de memoria de forma controlada.
 * Devuelve cuántas páginas se expulsaron.
 */
uint32_t dsm_coherence_force_eviction(dsm_coherence_t *co, uint32_t count);

#endif /* DSM_COHERENCE_H */
