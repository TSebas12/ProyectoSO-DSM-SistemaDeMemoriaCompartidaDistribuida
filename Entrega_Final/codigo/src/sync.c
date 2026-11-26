/*
 * sync.c — Implementación de los envoltorios de sincronización.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * La comprobación de orden se lleva en una pila por hilo (thread-local). Si un
 * hilo toma un lock de clase menor que el que ya tiene, se registra la
 * violación y se imprime un aviso, pero la operación continúa: en el prototipo
 * interesa detectar el problema sin abortar la demostración.
 */
#include "dsm_sync.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>

/* --- Estado de la comprobación de orden -------------------------------- */
static _Thread_local dsm_lock_class_t t_stack[DSM_LOCK_STACK_MAX];
static _Thread_local int              t_depth;

static _Atomic int      g_check_enabled = 1;
static _Atomic uint64_t g_violations;

void dsm_sync_set_check(int enabled)
{
    atomic_store(&g_check_enabled, enabled ? 1 : 0);
}

uint64_t dsm_sync_violations(void)
{
    return atomic_load(&g_violations);
}

void dsm_sync_thread_reset(void)
{
    t_depth = 0;
}

static dsm_lock_class_t top_class(void)
{
    int i;

    if (t_depth <= 0) {
        return (dsm_lock_class_t)-1;
    }
    i = t_depth - 1;
    if (i >= DSM_LOCK_STACK_MAX) {
        i = DSM_LOCK_STACK_MAX - 1;
    }
    return t_stack[i];
}

static void push_class(dsm_lock_class_t klass)
{
    if (t_depth < DSM_LOCK_STACK_MAX) {
        t_stack[t_depth] = klass;
    }
    t_depth++;
}

static void pop_class(void)
{
    if (t_depth > 0) {
        t_depth--;
    }
}

static void check_order(dsm_lock_class_t klass, const char *name)
{
    dsm_lock_class_t prev;

    if (atomic_load(&g_check_enabled)) {
        prev = top_class();
        if (prev != (dsm_lock_class_t)-1 && klass < prev) {
            atomic_fetch_add(&g_violations, 1);
            fprintf(stderr,
                    "[dsm][sync] orden de locks invalido: se toma '%s' (clase %d) "
                    "teniendo clase %d tomada\n",
                    name != NULL ? name : "?", (int)klass, (int)prev);
        }
    }
    /*
     * La pila se actualiza siempre, aunque la comprobación esté apagada: si no,
     * el desbalance entre push y pop dejaría el diagnóstico en un estado falso.
     */
    push_class(klass);
}

/* --- Mutex -------------------------------------------------------------- */
dsm_status_t dsm_mutex_init(dsm_mutex_t *m, dsm_lock_class_t klass, const char *name)
{
    int rc;

    if (m == NULL) {
        return DSM_ERR_INVAL;
    }
    rc = pthread_mutex_init(&m->handle, NULL);
    if (rc != 0) {
        return DSM_ERR_NOMEM;
    }
    m->klass = klass;
    m->name = name;
    m->initialized = 1;
    return DSM_OK;
}

dsm_status_t dsm_mutex_destroy(dsm_mutex_t *m)
{
    int rc;

    if (m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    rc = pthread_mutex_destroy(&m->handle);
    m->initialized = 0;
    return (rc == 0) ? DSM_OK : DSM_ERR_BUSY;
}

dsm_status_t dsm_mutex_lock(dsm_mutex_t *m)
{
    int rc;

    if (m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    rc = pthread_mutex_lock(&m->handle);
    if (rc != 0) {
        return (rc == EDEADLK) ? DSM_ERR_BUSY : DSM_ERR_STATE;
    }
    check_order(m->klass, m->name);
    return DSM_OK;
}

dsm_status_t dsm_mutex_trylock(dsm_mutex_t *m)
{
    int rc;

    if (m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    rc = pthread_mutex_trylock(&m->handle);
    if (rc == EBUSY) {
        return DSM_ERR_BUSY;
    }
    if (rc != 0) {
        return DSM_ERR_STATE;
    }
    check_order(m->klass, m->name);
    return DSM_OK;
}

dsm_status_t dsm_mutex_unlock(dsm_mutex_t *m)
{
    int rc;

    if (m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    rc = pthread_mutex_unlock(&m->handle);
    if (rc != 0) {
        return DSM_ERR_STATE;
    }
    pop_class();
    return DSM_OK;
}

/* --- Variables de condición --------------------------------------------- */
dsm_status_t dsm_cond_init(dsm_cond_t *c)
{
    if (c == NULL) {
        return DSM_ERR_INVAL;
    }
    if (pthread_cond_init(&c->handle, NULL) != 0) {
        return DSM_ERR_NOMEM;
    }
    c->initialized = 1;
    return DSM_OK;
}

dsm_status_t dsm_cond_destroy(dsm_cond_t *c)
{
    if (c == NULL || !c->initialized) {
        return DSM_ERR_INVAL;
    }
    if (pthread_cond_destroy(&c->handle) != 0) {
        return DSM_ERR_BUSY;
    }
    c->initialized = 0;
    return DSM_OK;
}

dsm_status_t dsm_cond_wait(dsm_cond_t *c, dsm_mutex_t *m)
{
    int rc;

    if (c == NULL || !c->initialized || m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    /*
     * Se descuenta la clase del lock mientras se espera: la condición libera el
     * mutex, así que el hilo deja de tenerlo y el orden no debe contarse.
     */
    pop_class();
    rc = pthread_cond_wait(&c->handle, &m->handle);
    push_class(m->klass);
    return (rc == 0) ? DSM_OK : DSM_ERR_STATE;
}

dsm_status_t dsm_cond_timedwait(dsm_cond_t *c, dsm_mutex_t *m, uint32_t timeout_ms)
{
    struct timespec deadline;
    int rc;

    if (c == NULL || !c->initialized || m == NULL || !m->initialized) {
        return DSM_ERR_INVAL;
    }
    dsm_deadline_after_ms(&deadline, timeout_ms);
    pop_class();
    rc = pthread_cond_timedwait(&c->handle, &m->handle, &deadline);
    push_class(m->klass);
    if (rc == ETIMEDOUT) {
        return DSM_ERR_TIMEOUT;
    }
    return (rc == 0) ? DSM_OK : DSM_ERR_STATE;
}

dsm_status_t dsm_cond_signal(dsm_cond_t *c)
{
    if (c == NULL || !c->initialized) {
        return DSM_ERR_INVAL;
    }
    return (pthread_cond_signal(&c->handle) == 0) ? DSM_OK : DSM_ERR_STATE;
}

dsm_status_t dsm_cond_broadcast(dsm_cond_t *c)
{
    if (c == NULL || !c->initialized) {
        return DSM_ERR_INVAL;
    }
    return (pthread_cond_broadcast(&c->handle) == 0) ? DSM_OK : DSM_ERR_STATE;
}

/* --- Semáforos ---------------------------------------------------------- */
dsm_status_t dsm_sem_init(dsm_sem_t *s, unsigned int value)
{
    if (s == NULL) {
        return DSM_ERR_INVAL;
    }
    if (sem_init(&s->handle, 0, value) != 0) {
        return DSM_ERR_NOMEM;
    }
    s->initialized = 1;
    return DSM_OK;
}

dsm_status_t dsm_sem_destroy(dsm_sem_t *s)
{
    if (s == NULL || !s->initialized) {
        return DSM_ERR_INVAL;
    }
    if (sem_destroy(&s->handle) != 0) {
        return DSM_ERR_BUSY;
    }
    s->initialized = 0;
    return DSM_OK;
}

dsm_status_t dsm_sem_wait(dsm_sem_t *s)
{
    if (s == NULL || !s->initialized) {
        return DSM_ERR_INVAL;
    }
    while (sem_wait(&s->handle) != 0) {
        if (errno != EINTR) {
            return DSM_ERR_STATE;
        }
    }
    return DSM_OK;
}

dsm_status_t dsm_sem_timedwait(dsm_sem_t *s, uint32_t timeout_ms)
{
    struct timespec deadline;

    if (s == NULL || !s->initialized) {
        return DSM_ERR_INVAL;
    }
    if (timeout_ms == 0) {
        return dsm_sem_trywait(s);
    }
    dsm_deadline_after_ms(&deadline, timeout_ms);
    while (sem_timedwait(&s->handle, &deadline) != 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == ETIMEDOUT) {
            return DSM_ERR_TIMEOUT;
        }
        return DSM_ERR_STATE;
    }
    return DSM_OK;
}

dsm_status_t dsm_sem_post(dsm_sem_t *s)
{
    if (s == NULL || !s->initialized) {
        return DSM_ERR_INVAL;
    }
    return (sem_post(&s->handle) == 0) ? DSM_OK : DSM_ERR_STATE;
}

dsm_status_t dsm_sem_trywait(dsm_sem_t *s)
{
    if (s == NULL || !s->initialized) {
        return DSM_ERR_INVAL;
    }
    if (sem_trywait(&s->handle) != 0) {
        return (errno == EAGAIN) ? DSM_ERR_BUSY : DSM_ERR_STATE;
    }
    return DSM_OK;
}

/* --- Utilidades de tiempo ----------------------------------------------- */
uint64_t dsm_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

void dsm_deadline_after_ms(struct timespec *ts, uint32_t timeout_ms)
{
    struct timespec now;

    if (ts == NULL) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &now);
    ts->tv_sec  = now.tv_sec + (time_t)(timeout_ms / 1000u);
    ts->tv_nsec = now.tv_nsec + (long)(timeout_ms % 1000u) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec  += 1;
        ts->tv_nsec -= 1000000000L;
    }
}
