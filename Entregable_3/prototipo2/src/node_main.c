/*
 * node_main.c — Binario del nodo DSM.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Uso:
 *   ./dsm_node --config config/node0.conf                 (queda escuchando)
 *   ./dsm_node --config config/node1.conf --app demo
 *   ./dsm_node --config config/node0.conf --app matrices --size 128
 *   ./dsm_node --config config/node0.conf --app file --file datos.bin
 *   ./dsm_node --config config/node0.conf --check-config
 *
 * Sin --app el nodo se queda abierto atendiendo RPCs, que es lo que permite
 * probar de una PC a otra sin relanzar el proceso por cada mensaje. Con --app
 * ejecuta la aplicación y, si además se pasa --serve, se queda abierto después.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dsm_api.h"
#include "dsm_apps.h"
#include "dsm_config.h"
#include "dsm_log.h"
#include "dsm_nodes.h"
#include "dsm_runtime.h"
#include "dsm_sync.h"

static volatile sig_atomic_t g_stop = 0;

static void on_stop_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void usage(const char *prog)
{
    printf("uso: %s [opciones]\n"
           "  --config RUTA     archivo del nodo (def. config/node0.conf)\n"
           "  --nodes RUTA      tabla de nodos (def. config/nodes.conf)\n"
           "  --app NOMBRE      demo|matrices|file|counter|bench|evict\n"
           "  --size N          lado de la matriz o bytes del archivo\n"
           "  --pages N         paginas para bench/evict\n"
           "  --iters N         iteraciones de las aplicaciones\n"
           "  --threads N       hilos por nodo (counter)\n"
           "  --file RUTA       archivo de entrada para --app file\n"
           "  --frames N        fuerza N marcos por nodo (pruebas de expulsion)\n"
           "  --no-verify       omite la verificacion de matrices/file\n"
           "  --serve           queda escuchando despues de la aplicacion\n"
           "  --check-config    valida la configuracion y sale\n"
           "  --version         muestra la version\n",
           prog);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
    const char    *config_path = "config/node0.conf";
    const char    *nodes_path  = "config/nodes.conf";
    const char    *app_name    = NULL;
    dsm_app_opts_t opts;
    dsm_config_t   cfg;
    dsm_nodes_t    nodes;
    dsm_runtime_t *rt = NULL;
    dsm_app_kind_t app = DSM_APP_NONE;
    uint32_t       frames_override = 0;
    int            serve = 0;
    int            check_only = 0;
    int            i;

    /*
     * Salida línea a línea: cuando el nodo corre en segundo plano la salida va
     * a un archivo y, sin esto, un SIGTERM se llevaría el búfer sin escribir.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    dsm_app_opts_default(&opts);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc) {
            nodes_path = argv[++i];
        } else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            app_name = argv[++i];
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            opts.size = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--pages") == 0 && i + 1 < argc) {
            opts.pages = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            opts.iters = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            opts.threads = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            opts.file = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames_override = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--serve") == 0) {
            serve = 1;
        } else if (strcmp(argv[i], "--check-config") == 0) {
            check_only = 1;
        } else if (strcmp(argv[i], "--no-verify") == 0) {
            opts.verify = 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("dsm_node 2.0 (prototipo 2, protocolo DSM2)\n");
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "opcion desconocida: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (dsm_config_load(config_path, &cfg) != DSM_OK) {
        fprintf(stderr, "no se pudo leer la configuracion %s\n", config_path);
        return 1;
    }
    if (frames_override != 0) {
        cfg.node_memory = (uint64_t)frames_override * (uint64_t)cfg.page_size;
    }
    if (dsm_config_validate(&cfg) != DSM_OK) {
        fprintf(stderr, "configuracion invalida: %s\n", config_path);
        return 1;
    }
    if (dsm_nodes_load(nodes_path, cfg.node_id, cfg.listen_port, &nodes) != DSM_OK) {
        fprintf(stderr, "no se pudo leer la tabla de nodos %s (nodo %u)\n",
                nodes_path, cfg.node_id);
        return 1;
    }

    if (check_only) {
        printf("%s: OK (nodo %u, %u nodos en %s)\n",
               config_path, cfg.node_id, nodes.count, nodes_path);
        dsm_nodes_print(&nodes, stdout);
        return 0;
    }

    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "aviso: no se pudo crear logs/\n");
    }
    dsm_log_init(cfg.log_file, (int)cfg.log_level);
    install_signals();

    printf("nodo %u: arrancando (home=%u, %u nodos, puerto %u)\n",
           cfg.node_id, 0u, nodes.count, (unsigned)cfg.listen_port);

    rt = calloc(1, sizeof(*rt));
    if (rt == NULL) {
        fprintf(stderr, "sin memoria para el runtime\n");
        return 1;
    }
    if (dsm_runtime_init(rt, &cfg, &nodes) != DSM_OK) {
        fprintf(stderr, "nodo %u: fallo el arranque del runtime\n", cfg.node_id);
        free(rt);
        return 1;
    }
    if (dsm_attach(rt) != DSM_OK) {
        fprintf(stderr, "nodo %u: no se pudo registrar el hilo principal\n",
                cfg.node_id);
        dsm_runtime_destroy(rt);
        free(rt);
        return 1;
    }
    if (dsm_runtime_start_services(rt) != DSM_OK) {
        fprintf(stderr, "nodo %u: no se pudieron levantar los servicios\n",
                cfg.node_id);
    }

    printf("nodo %u: listo. control=%u web=%u (%s)\n",
           cfg.node_id, (unsigned)cfg.control_port, (unsigned)cfg.web_port,
           cfg.web_enabled ? "activa" : "apagada");

    if (app_name != NULL) {
        app = dsm_app_parse(app_name);
        if (app == DSM_APP_NONE) {
            fprintf(stderr, "aplicacion desconocida: %s\n", app_name);
            dsm_runtime_destroy(rt);
            free(rt);
            return 2;
        }
        printf("nodo %u: ejecutando aplicacion '%s'\n", cfg.node_id, app_name);
        if (app == DSM_APP_DEMO) {
            dsm_demo_page_run(rt);
        } else if (dsm_app_run(rt, app, &opts) != 0) {
            fprintf(stderr, "nodo %u: la aplicacion '%s' reporto un fallo\n",
                    cfg.node_id, app_name);
        }
        dsm_runtime_print_stats(rt, stdout);
    }

    if (serve || app_name == NULL) {
        printf("nodo %u: escuchando; Ctrl+C para detener\n", cfg.node_id);
        while (!g_stop) {
            sleep_ms(100);
        }
        printf("nodo %u: deteniendo...\n", cfg.node_id);
    }

    dsm_detach(rt);
    dsm_runtime_destroy(rt);
    free(rt);
    dsm_log_close();
    return 0;
}
