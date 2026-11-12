/*
 * demo_page.c — Demostración de una página compartida entre dos nodos.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Autor    : Sebastián Agüero Brenes
 * Fecha    : 2026-11-10
 *
 * El nodo 0 (home) crea el objeto de 4 páginas. El nodo 1 provoca un fallo de
 * lectura (I -> S), uno de escritura (S -> M) y devuelve la página con un
 * writeback. El nodo 0 espera ese writeback consultando el directorio, sin
 * tocar la ventana mientras la página está inválida.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dsm_coherence.h"
#include "dsm_memory.h"
#include "dsm_runtime.h"
#include "dsm_stats.h"
#include "dsm_sync.h"
#include "dsm_types.h"

/* Tiempo máximo que el home espera el writeback del otro nodo. */
#define DSM_DEMO_WAIT_MS 15000u

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static void print_stats(dsm_runtime_t *rt)
{
    char buf[512];
    uint64_t resident = 0;
    uint64_t free_frames = 0;

    /*
     * Residentes, marcos libres y tiempo activo son valores instantáneos, no
     * contadores acumulados: se toman justo antes de imprimir.
     */
    dsm_memory_usage(&rt->mem, &resident, &free_frames);
    dsm_stats_set(&rt->stats, DSM_STAT_PAGES_RESIDENT, resident);
    dsm_stats_set(&rt->stats, DSM_STAT_FRAMES_FREE, free_frames);
    dsm_stats_set(&rt->stats, DSM_STAT_UPTIME_MS, dsm_now_ms() - rt->start_ms);

    if (dsm_stats_format(&rt->stats, buf, sizeof(buf)) > 0) {
        printf("nodo %u: contadores\n%s\n", rt->cfg.node_id, buf);
    }
}

/* --- Nodo home ---------------------------------------------------------- */
static void demo_home(dsm_runtime_t *rt)
{
    dsm_page_snapshot_t snap;
    char *p;
    uint64_t deadline;
    int listo = 0;

    p = dsm_memory_page_addr(&rt->mem, 0);
    if (p == NULL) {
        printf("nodo 0: la pagina 0 no esta en la ventana\n");
        return;
    }

    printf("nodo 0: lee pagina 0 -> \"%s\"\n", p);
    printf("nodo 0: esperando el writeback del nodo 1...\n");
    fflush(stdout);

    /*
     * El home no puede leer la pagina mientras la tenga otro nodo: la ventana
     * esta en PROT_NONE. Se espera a que el writeback la devuelva (version
     * mayor y residente) antes de volver a tocarla.
     */
    deadline = dsm_now_ms() + DSM_DEMO_WAIT_MS;
    while (dsm_now_ms() < deadline) {
        if (dsm_coherence_snapshot(&rt->co, 0, &snap) == DSM_OK &&
            snap.residency == (uint8_t)DSM_RES_RESIDENT &&
            snap.version >= 3u) {
            listo = 1;
            break;
        }
        sleep_ms(200);
    }

    if (!listo) {
        printf("nodo 0: no llego el writeback en %u ms\n", DSM_DEMO_WAIT_MS);
        return;
    }

    p = dsm_memory_page_addr(&rt->mem, 0);
    printf("nodo 0: tras el writeback, pagina 0 -> \"%s\"\n", p);
    fflush(stdout);
}

/* --- Nodo cliente ------------------------------------------------------- */
static void demo_client(dsm_runtime_t *rt)
{
    uint32_t page_size = dsm_memory_page_size(&rt->mem);
    dsm_page_entry_t *page;
    dsm_status_t st;
    char *p;

    /* Deja que el home cree el objeto antes del primer acceso. */
    sleep_ms(1500);

    p = dsm_memory_page_addr(&rt->mem, 0);
    if (p == NULL) {
        printf("nodo 1: la pagina 0 no esta en la ventana\n");
        return;
    }

    /* Primer acceso: la ventana esta en PROT_NONE -> SIGSEGV -> I a S. */
    printf("nodo 1: lee pagina 0 (fallo remoto de lectura)...\n");
    fflush(stdout);
    printf("nodo 1: contenido -> \"%s\"\n", p);
    fflush(stdout);

    /* La pagina esta en S (solo lectura): escribir dispara S a M. */
    printf("nodo 1: escribe pagina 0 (fallo remoto de escritura)...\n");
    fflush(stdout);
    snprintf(p, page_size, "pagina 0 escrita por el nodo %u (pid %ld)",
             rt->cfg.node_id, (long)getpid());
    printf("nodo 1: contenido nuevo -> \"%s\"\n", p);
    fflush(stdout);

    /* Tercer acceso: residente y propia, no hay fallo ni viaje a la red. */
    printf("nodo 1: relee pagina 0 (residente, sin fallo) -> \"%s\"\n", p);
    fflush(stdout);

    printf("nodo 1: devuelve la pagina 0 al home (writeback)...\n");
    fflush(stdout);
    st = rt->co.ops.writeback(rt->co.ops.ctx, 0, p, page_size);
    if (st != DSM_OK) {
        printf("nodo 1: writeback fallo: %s\n", dsm_status_name(st));
        fflush(stdout);
        return;
    }
    printf("nodo 1: writeback OK\n");

    /* El home ya tiene la copia: aqui la local pasa a limpia y compartida. */
    page = dsm_memory_page_lookup(&rt->mem, 0);
    if (page != NULL && dsm_mutex_lock(&page->lock) == DSM_OK) {
        page->meta.msi_state = (uint8_t)DSM_PAGE_S;
        page->meta.is_dirty  = 0;
        page->meta.version++;
        dsm_mutex_unlock(&page->lock);
        dsm_memory_protect(&rt->mem, 0, DSM_PAGE_S);
    }
    fflush(stdout);
}

void dsm_demo_page_run(dsm_runtime_t *rt)
{
    if (rt == NULL) {
        return;
    }

    printf("=== demo DSM: nodo %u (home %u) ===\n",
           rt->cfg.node_id, rt->co.home_node);
    fflush(stdout);

    if (rt->cfg.node_id == rt->co.home_node) {
        demo_home(rt);
    } else {
        demo_client(rt);
    }

    print_stats(rt);
    fflush(stdout);
}
