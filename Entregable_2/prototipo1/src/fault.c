/*
 * fault.c — Manejador de SIGSEGV y cola de fallos de página.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * Decisiones que no son obvias:
 *   - La cola es un anillo sin locks. Un pthread_mutex_lock dentro de un
 *     manejador de señal puede interbloquear si el hilo interrumpido ya tenía
 *     ese mutex tomado, así que no se usa ninguno.
 *   - El hilo no espera con sem_wait: sem_wait no es segura para señales. Se
 *     duerme con sigsuspend y el resolutor lo despierta con pthread_kill.
 *   - Si la dirección no es de la ventana, o el fallo no es de protección, se
 *     restaura el manejador anterior y se relanza la señal: es un error real
 *     del programa y no debe quedar oculto.
 */
#include "dsm_fault.h"

#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "dsm_sync.h"

/* --- Estado global del módulo ------------------------------------------- */
typedef struct dsm_fault_slot {
    dsm_fault_req_t req;
    _Atomic int     ready;
} dsm_fault_slot_t;

static struct {
    int               installed;
    dsm_fault_map_t   map;
    struct sigaction  prev_segv;
    struct sigaction  prev_usr1;
    size_t            altstack_size;

    dsm_fault_slot_t *slots;
    uint32_t          cap;      /* Potencia de dos                        */
    _Atomic uint64_t  head;     /* Índice reservado por los productores   */
    _Atomic uint64_t  tail;     /* Índice consumido                       */
    dsm_sem_t         items;
    _Atomic int       shutdown;
} g_q;

static _Thread_local dsm_thread_ctx_t *t_ctx;

/* Declaraciones adelantadas: la pila alternativa se usa en la desinstalación
 * y se define más abajo, junto al registro de hilos. */
static dsm_status_t altstack_install_current_thread(void);
static void         altstack_remove_current_thread(void);

/* --- Utilidades --------------------------------------------------------- */
static uint32_t next_pow2(uint32_t v)
{
    uint32_t p = 1;

    while (p < v && p < 0x40000000u) {
        p <<= 1;
    }
    return p;
}

static uint64_t map_addr_to_page(const void *addr)
{
    uintptr_t a;
    uintptr_t base;
    uint64_t  index;

    if (addr == NULL || g_q.map.window_base == NULL) {
        return DSM_INVALID_PAGE_ID;
    }
    a    = (uintptr_t)addr;
    base = (uintptr_t)g_q.map.window_base;

    if (a < base || a >= base + (uintptr_t)g_q.map.window_size) {
        return DSM_INVALID_PAGE_ID;
    }
    index = (uint64_t)((a - base) / (uintptr_t)g_q.map.page_size);
    return g_q.map.page_base_id + index;
}

/*
 * x86-64 deja el motivo del fallo en REG_ERR: el bit 1 indica escritura.
 * En otras arquitecturas se asume escritura, que es la política conservadora
 * descrita en el E1 (se pide propiedad en lugar de una copia compartida).
 */
static dsm_fault_kind_t decode_kind(void *uctx)
{
#if defined(__x86_64__)
    ucontext_t *uc = (ucontext_t *)uctx;

    if (uc != NULL && (uc->uc_mcontext.gregs[REG_ERR] & 0x2) != 0) {
        return DSM_FAULT_WRITE;
    }
    return DSM_FAULT_READ;
#else
    (void)uctx;
    return DSM_FAULT_UNKNOWN;
#endif
}

/* Restaura el manejador anterior y relanza: la señal no era del DSM.      */
static void reraise(int sig)
{
    sigset_t unblock;

    if (sig == SIGSEGV) {
        sigaction(SIGSEGV, &g_q.prev_segv, NULL);
    }
    sigemptyset(&unblock);
    sigaddset(&unblock, sig);
    sigprocmask(SIG_UNBLOCK, &unblock, NULL);
    raise(sig);
}

/* --- Cola sin locks ----------------------------------------------------- */
static int queue_push(const dsm_fault_req_t *req)
{
    for (;;) {
        uint64_t head = atomic_load_explicit(&g_q.head, memory_order_relaxed);
        uint64_t tail = atomic_load_explicit(&g_q.tail, memory_order_acquire);
        dsm_fault_slot_t *slot;

        if (head - tail >= (uint64_t)g_q.cap) {
            /*
             * Anillo lleno: el resolutor está atrasado. Se cede la CPU y se
             * reintenta en lugar de descartar el fallo, porque descartarlo
             * dejaría al hilo dormido para siempre.
             */
            if (atomic_load_explicit(&g_q.shutdown, memory_order_relaxed)) {
                return 0;
            }
            sched_yield();
            continue;
        }
        if (!atomic_compare_exchange_weak_explicit(&g_q.head, &head, head + 1,
                                                   memory_order_acq_rel,
                                                   memory_order_relaxed)) {
            continue;
        }
        slot = &g_q.slots[head & (uint64_t)(g_q.cap - 1)];
        slot->req = *req;
        atomic_store_explicit(&slot->ready, 1, memory_order_release);
        dsm_sem_post(&g_q.items);
        return 1;
    }
}

/* --- Manejadores -------------------------------------------------------- */
static void dsm_resume_handler(int sig)
{
    (void)sig;
}

static void dsm_segv_handler(int sig, siginfo_t *si, void *uctx)
{
    dsm_thread_ctx_t *ctx;
    dsm_fault_req_t   req;
    sigset_t          block;
    sigset_t          oldmask;
    sigset_t          waitmask;
    uint64_t          page_id;

    ctx = t_ctx;

    /* Fuera de la ventana, sin contexto o con un fallo ya en curso: no es   */
    /* un fallo que el DSM pueda resolver.                                   */
    if (!g_q.installed || ctx == NULL || si == NULL) {
        reraise(sig);
        return;
    }
    if (si->si_code != SEGV_ACCERR) {
        reraise(sig);
        return;
    }
    if (atomic_load_explicit(&ctx->in_fault, memory_order_relaxed) != 0 ||
        atomic_load_explicit(&ctx->failed, memory_order_relaxed) != 0) {
        reraise(sig);
        return;
    }

    page_id = map_addr_to_page(si->si_addr);
    if (page_id == DSM_INVALID_PAGE_ID) {
        reraise(sig);
        return;
    }

    /*
     * Se bloquea DSM_SIG_RESUME antes de publicar in_fault. Si el resolutor
     * despierta al hilo justo después, la señal queda pendiente y sigsuspend
     * retorna de inmediato; no se pierde.
     *
     * Se usa sigprocmask y no pthread_sigmask porque esta llamada ocurre dentro
     * del manejador y solo sigprocmask está en la lista de funciones seguras
     * para señales. En Linux ambas afectan únicamente al hilo que las llama.
     */
    sigemptyset(&block);
    sigaddset(&block, SIGSEGV);
    sigaddset(&block, DSM_SIG_RESUME);
    sigprocmask(SIG_BLOCK, &block, &oldmask);

    ctx->pending_page    = page_id;
    ctx->pending_request = 0;
    ctx->fault_kind      = (int)decode_kind(uctx);
    atomic_store_explicit(&ctx->in_fault, 1, memory_order_seq_cst);

    req.page_id        = page_id;
    req.thread_id      = 0;
    req.request_id_low = 0;
    req.kind           = (dsm_fault_kind_t)ctx->fault_kind;
    req.fault_addr     = si->si_addr;
    req.ctx            = ctx;

    if (!queue_push(&req)) {
        atomic_store_explicit(&ctx->in_fault, 0, memory_order_seq_cst);
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        reraise(sig);
        return;
    }

    waitmask = oldmask;
    sigaddset(&waitmask, SIGSEGV);
    sigdelset(&waitmask, DSM_SIG_RESUME);

    while (atomic_load_explicit(&ctx->in_fault, memory_order_seq_cst) != 0) {
        sigsuspend(&waitmask);
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    /* Si la página no se pudo resolver, el reintento vuelve a fallar y aquí  */
    /* se deja morir el proceso con el SIGSEGV original.                     */
    if (atomic_load_explicit(&ctx->failed, memory_order_seq_cst) != 0) {
        reraise(sig);
    }
}

/* --- Instalación -------------------------------------------------------- */
dsm_status_t dsm_fault_install(const dsm_fault_map_t *map, const dsm_fault_params_t *params)
{
    dsm_fault_params_t defaults;
    struct sigaction sa;
    uint32_t queue_size;
    size_t altstack_size;

    if (map == NULL || map->window_base == NULL || map->page_size == 0) {
        return DSM_ERR_INVAL;
    }
    if (g_q.installed) {
        return DSM_ERR_STATE;
    }

    if (params == NULL) {
        defaults.queue_size = 4096;
        defaults.altstack_size = 0;
        params = &defaults;
    }

    queue_size = (params->queue_size == 0) ? 4096u : next_pow2(params->queue_size);
    if (queue_size < 2u) {
        queue_size = 2u;
    }

    memset(&g_q, 0, sizeof(g_q));
    g_q.map = *map;

    g_q.slots = calloc(queue_size, sizeof(dsm_fault_slot_t));
    if (g_q.slots == NULL) {
        return DSM_ERR_NOMEM;
    }
    g_q.cap = queue_size;
    atomic_init(&g_q.head, 0);
    atomic_init(&g_q.tail, 0);
    atomic_init(&g_q.shutdown, 0);

    if (dsm_sem_init(&g_q.items, 0) != DSM_OK) {
        free(g_q.slots);
        g_q.slots = NULL;
        return DSM_ERR_NOMEM;
    }

    altstack_size = (params->altstack_size == 0) ? 65536u : params->altstack_size;
    if (altstack_size < (size_t)MINSIGSTKSZ * 2u) {
        altstack_size = (size_t)MINSIGSTKSZ * 2u;
    }
    g_q.altstack_size = altstack_size;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = dsm_segv_handler;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &g_q.prev_segv) != 0) {
        dsm_sem_destroy(&g_q.items);
        free(g_q.slots);
        g_q.slots = NULL;
        return DSM_ERR_SIGACTION;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = dsm_resume_handler;
    sa.sa_flags   = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(DSM_SIG_RESUME, &sa, &g_q.prev_usr1) != 0) {
        sigaction(SIGSEGV, &g_q.prev_segv, NULL);
        dsm_sem_destroy(&g_q.items);
        free(g_q.slots);
        g_q.slots = NULL;
        return DSM_ERR_SIGACTION;
    }

    g_q.installed = 1;
    return DSM_OK;
}

void dsm_fault_uninstall(void)
{
    if (!g_q.installed) {
        return;
    }
    g_q.installed = 0;

    sigaction(SIGSEGV, &g_q.prev_segv, NULL);
    sigaction(DSM_SIG_RESUME, &g_q.prev_usr1, NULL);

    altstack_remove_current_thread();
    dsm_sem_destroy(&g_q.items);
    free(g_q.slots);
    g_q.slots = NULL;
}

/* --- Pila alternativa por hilo ------------------------------------------ */
/*
 * sigaltstack es por hilo. Si todos los hilos compartieran un solo buffer, dos
 * fallos simultáneos escribirían la misma pila y el proceso moriría. Por eso
 * cada hilo registrado reserva la suya y la libera al terminar.
 */
static pthread_key_t  g_altstack_key;
static pthread_once_t g_altstack_once = PTHREAD_ONCE_INIT;
static int            g_altstack_key_ok;

static void altstack_destroy(void *p)
{
    free(p);
}

static void altstack_key_create(void)
{
    if (pthread_key_create(&g_altstack_key, altstack_destroy) == 0) {
        g_altstack_key_ok = 1;
    }
}

static dsm_status_t altstack_install_current_thread(void)
{
    stack_t ss;
    void   *mem;
    size_t  sz;

    pthread_once(&g_altstack_once, altstack_key_create);
    if (!g_altstack_key_ok) {
        return DSM_ERR_NOMEM;
    }
    if (pthread_getspecific(g_altstack_key) != NULL) {
        return DSM_OK;
    }

    sz = (g_q.altstack_size == 0) ? 65536u : g_q.altstack_size;
    mem = malloc(sz);
    if (mem == NULL) {
        return DSM_ERR_NOMEM;
    }
    ss.ss_sp    = mem;
    ss.ss_size  = sz;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        free(mem);
        return DSM_ERR_SIGACTION;
    }
    if (pthread_setspecific(g_altstack_key, mem) != 0) {
        free(mem);
        return DSM_ERR_NOMEM;
    }
    return DSM_OK;
}

static void altstack_remove_current_thread(void)
{
    stack_t ss;
    void   *mem;

    if (!g_altstack_key_ok) {
        return;
    }
    mem = pthread_getspecific(g_altstack_key);
    if (mem == NULL) {
        return;
    }
    ss.ss_sp    = NULL;
    ss.ss_size  = 0;
    ss.ss_flags = SS_DISABLE;
    sigaltstack(&ss, NULL);
    pthread_setspecific(g_altstack_key, NULL);
    free(mem);
}

/* --- Contexto por hilo -------------------------------------------------- */
dsm_status_t dsm_fault_attach_thread(dsm_thread_ctx_t *ctx)
{
    if (ctx == NULL) {
        return DSM_ERR_INVAL;
    }
    if (!g_q.installed) {
        return DSM_ERR_STATE;
    }
    if (altstack_install_current_thread() != DSM_OK) {
        return DSM_ERR_NOMEM;
    }
    ctx->tid = pthread_self();
    ctx->pending_page = DSM_INVALID_PAGE_ID;
    ctx->pending_request = 0;
    ctx->fault_kind = (int)DSM_FAULT_UNKNOWN;
    atomic_init(&ctx->in_fault, 0);
    atomic_init(&ctx->failed, 0);
    dsm_sync_thread_reset();
    t_ctx = ctx;
    return DSM_OK;
}

void dsm_fault_detach_thread(void)
{
    t_ctx = NULL;
}

dsm_thread_ctx_t *dsm_fault_thread_ctx(void)
{
    return t_ctx;
}

/* --- Cola de fallos ----------------------------------------------------- */
dsm_status_t dsm_fault_queue_pop(dsm_fault_req_t *out, uint32_t timeout_ms)
{
    dsm_status_t st;
    uint64_t     tail;
    dsm_fault_slot_t *slot;

    if (out == NULL) {
        return DSM_ERR_INVAL;
    }
    if (!g_q.installed) {
        return DSM_ERR_STATE;
    }

    st = (timeout_ms == UINT32_MAX) ? dsm_sem_wait(&g_q.items)
                                    : dsm_sem_timedwait(&g_q.items, timeout_ms);
    if (st != DSM_OK) {
        return st;
    }
    if (atomic_load_explicit(&g_q.shutdown, memory_order_acquire) != 0) {
        return DSM_ERR_SHUTDOWN;
    }

    tail = atomic_load_explicit(&g_q.tail, memory_order_relaxed);
    slot = &g_q.slots[tail & (uint64_t)(g_q.cap - 1)];

    /*
     * Otro productor pudo reservar este índice antes y no haberlo escrito
     * todavía; se espera a que lo publique.
     */
    while (atomic_load_explicit(&slot->ready, memory_order_acquire) == 0) {
        sched_yield();
    }

    *out = slot->req;
    atomic_store_explicit(&slot->ready, 0, memory_order_release);
    atomic_store_explicit(&g_q.tail, tail + 1, memory_order_release);
    return DSM_OK;
}

uint64_t dsm_fault_queued(void)
{
    uint64_t head = atomic_load_explicit(&g_q.head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&g_q.tail, memory_order_acquire);

    return (head > tail) ? (head - tail) : 0;
}

void dsm_fault_shutdown(void)
{
    if (!g_q.installed) {
        return;
    }
    /* Idempotente: un segundo aviso dejaría un semáforo de más y el
     * consumidor se quedaría esperando un fallo que no existe. */
    if (atomic_exchange_explicit(&g_q.shutdown, 1, memory_order_release) == 0) {
        dsm_sem_post(&g_q.items);
    }
}

/* --- Resolución --------------------------------------------------------- */
void dsm_fault_wake(dsm_thread_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    atomic_store_explicit(&ctx->in_fault, 0, memory_order_seq_cst);
    pthread_kill(ctx->tid, DSM_SIG_RESUME);
}

void dsm_fault_mark_failed(dsm_thread_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    atomic_store_explicit(&ctx->failed, 1, memory_order_seq_cst);
}
