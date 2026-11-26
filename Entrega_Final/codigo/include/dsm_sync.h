/*
 * dsm_sync.h — Primitivas de sincronización y jerarquía de locks.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * Envoltorios delgados sobre Pthreads y semáforos POSIX. Además de envolver,
 * verifican el orden de adquisición: un hilo nunca puede tomar un lock de
 * clase inferior al que ya tiene. Esa comprobación es la que evita los
 * interbloqueos por orden invertido.
 */
#ifndef DSM_SYNC_H
#define DSM_SYNC_H

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include "dsm_types.h"

/*
 * Jerarquía de locks, de mayor a menor alcance.
 *
 * Un hilo debe tomarlos siempre en orden creciente (GLOBAL -> OBJETO -> PAGINA
 * -> MARCO). Si toma PAGINA y después GLOBAL, hay riesgo de interbloqueo y la
 * comprobación lo reporta.
 */
typedef enum dsm_lock_class {
    DSM_LOCK_GLOBAL = 0,
    DSM_LOCK_OBJECT = 1,
    DSM_LOCK_PAGE   = 2,
    DSM_LOCK_FRAME  = 3
} dsm_lock_class_t;

#define DSM_LOCK_STACK_MAX 16

/* Mutex con clase y nombre, para poder diagnosticar el orden. */
typedef struct dsm_mutex {
    pthread_mutex_t  handle;
    dsm_lock_class_t klass;
    const char      *name;
    int              initialized;
} dsm_mutex_t;

/* Variable de condición asociada a un dsm_mutex_t. */
typedef struct dsm_cond {
    pthread_cond_t handle;
    int            initialized;
} dsm_cond_t;

/* Semáforo POSIX contado. */
typedef struct dsm_sem {
    sem_t handle;
    int   initialized;
} dsm_sem_t;

/* --- Mutex -------------------------------------------------------------- */
dsm_status_t dsm_mutex_init(dsm_mutex_t *m, dsm_lock_class_t klass, const char *name);
dsm_status_t dsm_mutex_destroy(dsm_mutex_t *m);
dsm_status_t dsm_mutex_lock(dsm_mutex_t *m);
dsm_status_t dsm_mutex_trylock(dsm_mutex_t *m);
dsm_status_t dsm_mutex_unlock(dsm_mutex_t *m);

/* --- Variables de condición --------------------------------------------- */
dsm_status_t dsm_cond_init(dsm_cond_t *c);
dsm_status_t dsm_cond_destroy(dsm_cond_t *c);
dsm_status_t dsm_cond_wait(dsm_cond_t *c, dsm_mutex_t *m);
dsm_status_t dsm_cond_timedwait(dsm_cond_t *c, dsm_mutex_t *m, uint32_t timeout_ms);
dsm_status_t dsm_cond_signal(dsm_cond_t *c);
dsm_status_t dsm_cond_broadcast(dsm_cond_t *c);

/* --- Semáforos ---------------------------------------------------------- */
dsm_status_t dsm_sem_init(dsm_sem_t *s, unsigned int value);
dsm_status_t dsm_sem_destroy(dsm_sem_t *s);
dsm_status_t dsm_sem_wait(dsm_sem_t *s);
dsm_status_t dsm_sem_timedwait(dsm_sem_t *s, uint32_t timeout_ms);
dsm_status_t dsm_sem_post(dsm_sem_t *s);
dsm_status_t dsm_sem_trywait(dsm_sem_t *s);

/* --- Diagnóstico del orden de locks ------------------------------------- */
/* Activa o desactiva la comprobación. Por defecto está activa.             */
void     dsm_sync_set_check(int enabled);
/* Devuelve cuántas violaciones de orden se detectaron desde el arranque.   */
uint64_t dsm_sync_violations(void);
/* Limpia la pila del hilo actual. Se llama al iniciar un hilo nuevo.       */
void     dsm_sync_thread_reset(void);

/* --- Utilidades de tiempo ----------------------------------------------- */
/* Milisegundos desde un instante arbitrario (CLOCK_MONOTONIC).            */
uint64_t dsm_now_ms(void);
/* Igual que dsm_now_ms, pero rellena un timespec para sem_timedwait.       */
void     dsm_deadline_after_ms(struct timespec *ts, uint32_t timeout_ms);

#endif /* DSM_SYNC_H */
