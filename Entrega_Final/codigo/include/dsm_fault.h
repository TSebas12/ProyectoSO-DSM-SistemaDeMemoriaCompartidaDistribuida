/*
 * dsm_fault.h — Manejador de SIGSEGV y cola de fallos de página.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * El manejador solo puede usar funciones seguras para señales. Por eso no toma
 * locks de pthread ni hace E/S: encola el fallo en un anillo sin locks, avisa
 * al resolutor con un semáforo y duerme al hilo hasta que lo despierten.
 */
#ifndef DSM_FAULT_H
#define DSM_FAULT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "dsm_types.h"

/* Copia de los datos de la ventana que necesita el manejador.            */
typedef struct dsm_fault_map {
    void    *window_base;
    size_t   window_size;
    uint32_t page_size;
    uint64_t page_base_id;
} dsm_fault_map_t;

typedef struct dsm_fault_params {
    uint32_t queue_size;     /* Se redondea a potencia de 2; 0 = 4096        */
    size_t   altstack_size;  /* Pila alternativa; 0 = 64 KB                 */
} dsm_fault_params_t;

/*
 * Contexto por hilo. `in_fault` y `failed` son atómicos porque los escribe el
 * resolutor y los lee el hilo que está dentro del manejador.
 */
typedef struct dsm_thread_ctx {
    pthread_t   tid;             /* Hilo que debe despertar                */
    uint64_t    pending_page;    /* Página que está esperando              */
    uint32_t    pending_request; /* Identificador de la solicitud          */
    _Atomic int in_fault;        /* 1 mientras el hilo duerme en el handler */
    int         fault_kind;      /* dsm_fault_kind_t                       */
    _Atomic int failed;          /* 1 si la resolución falló               */
} dsm_thread_ctx_t;

/* Elemento de la cola de fallos.                                          */
typedef struct dsm_fault_req {
    uint64_t          page_id;
    uint32_t          thread_id;
    uint32_t          request_id_low;
    dsm_fault_kind_t  kind;
    void             *fault_addr;
    dsm_thread_ctx_t *ctx;
} dsm_fault_req_t;

/* --- Instalación -------------------------------------------------------- */
dsm_status_t dsm_fault_install(const dsm_fault_map_t *map, const dsm_fault_params_t *params);
void         dsm_fault_uninstall(void);

/* --- Contexto por hilo -------------------------------------------------- */
/* Registra el contexto del hilo actual. Debe llamarse antes de tocar el DSM. */
dsm_status_t dsm_fault_attach_thread(dsm_thread_ctx_t *ctx);
void         dsm_fault_detach_thread(void);
dsm_thread_ctx_t *dsm_fault_thread_ctx(void);

/* --- Cola de fallos ----------------------------------------------------- */
/*
 * Extrae un fallo. timeout_ms = UINT32_MAX espera indefinidamente.
 * Devuelve DSM_ERR_SHUTDOWN cuando el runtime se está cerrando.
 */
dsm_status_t dsm_fault_queue_pop(dsm_fault_req_t *out, uint32_t timeout_ms);
/* Cantidad de fallos encolados y no consumidos.                          */
uint64_t     dsm_fault_queued(void);
/* Despierta al consumidor bloqueado y lo hace salir.                     */
void         dsm_fault_shutdown(void);

/* --- Resolución --------------------------------------------------------- */
/* Despierta al hilo que falló: baja in_fault y le manda DSM_SIG_RESUME.   */
void dsm_fault_wake(dsm_thread_ctx_t *ctx);
/* Marca el contexto como fallido: al reintentar, el proceso muere.        */
void dsm_fault_mark_failed(dsm_thread_ctx_t *ctx);

#endif /* DSM_FAULT_H */
