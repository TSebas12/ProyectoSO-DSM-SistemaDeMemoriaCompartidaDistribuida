/*
 * dsm_coherence.h — Protocolo MSI, directorio y resolución de fallos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * La coherencia no habla sockets: recibe y entrega dsm_msg_t y delega el
 * transporte en dsm_coherence_ops_t. Ese es el punto de unión con el módulo de
 * red (ver la guía del código, sección "Contrato con la capa de red").
 */
#ifndef DSM_COHERENCE_H
#define DSM_COHERENCE_H

#include <stddef.h>
#include <stdint.h>

#include "dsm_fault.h"
#include "dsm_memory.h"
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
 * send()            envía un mensaje sin esperar respuesta. No debe conservar
 *                   los punteros payload/data después de retornar: el
 *                   llamador los libera en cuanto send() vuelve.
 * fetch_shared()    RPC bloqueante: pide la página para lectura y deja los
 *                   datos en buf. Devuelve DSM_OK solo si buf es válido.
 * fetch_exclusive() RPC bloqueante: pide la propiedad de la página.
 * writeback()       devuelve una página sucia al home.
 *
 * Si un puntero es NULL, la operación se considera no disponible y la
 * coherencia devuelve DSM_ERR_NOTSUP en lugar de fallar.
 */
typedef struct dsm_coherence_ops {
    void         *ctx;
    dsm_status_t (*send)(void *ctx, const dsm_msg_t *msg);
    dsm_status_t (*fetch_shared)(void *ctx, uint64_t page_id, void *buf,
                                 size_t len, uint32_t timeout_ms);
    dsm_status_t (*fetch_exclusive)(void *ctx, uint64_t page_id, void *buf,
                                    size_t len, uint32_t timeout_ms);
    dsm_status_t (*writeback)(void *ctx, uint64_t page_id, const void *buf,
                              size_t len);
} dsm_coherence_ops_t;

typedef struct dsm_dir_entry {
    dsm_directory_entry_t  meta;
    struct dsm_dir_entry  *next;
} dsm_dir_entry_t;

typedef struct dsm_coherence_params {
    uint32_t node_id;      /* Nodo local                                  */
    uint32_t home_node;    /* Nodo que guarda el directorio (0 en el E2)  */
    uint32_t timeout_ms;   /* Espera de los RPC de página; 0 = 3000       */
    uint32_t dir_buckets;  /* Cubetas del directorio; 0 = 256             */
} dsm_coherence_params_t;

typedef struct dsm_coherence {
    dsm_memory_t       *mem;
    dsm_stats_t        *stats;
    uint32_t            node_id;
    uint32_t            home_node;
    uint32_t            timeout_ms;
    dsm_coherence_ops_t ops;

    dsm_dir_entry_t   **dir;
    uint32_t            dir_buckets;
    dsm_mutex_t         dir_lock;

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
 * Solo se llama con los mensajes que el home debe atender: PAGE_REQUEST,
 * OWNERSHIP_REQUEST, INVALIDATE y PAGE_WRITEBACK. Las respuestas de un RPC
 * las consume la capa de red y no llegan aquí.
 */
dsm_status_t dsm_coherence_handle_message(dsm_coherence_t *co, const dsm_msg_t *msg);

/* --- Consulta ----------------------------------------------------------- */
dsm_status_t dsm_coherence_snapshot(dsm_coherence_t *co, uint64_t page_id,
                                    dsm_page_snapshot_t *out);

#endif /* DSM_COHERENCE_H */
