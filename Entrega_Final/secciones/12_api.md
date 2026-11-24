# 12. API implementado

## 12.1 Tipos públicos

| Tipo | Definición | Uso |
|---|---|---|
| `dsm_status_t` | Código de estado (OK, ERR_*) | Retorno de todas las funciones |
| `dsm_runtime_t` | Contexto del runtime (config, nodos, memoria, red, coherencia) | Pasado a todas las funciones |
| `dsm_app_opts_t` | Opciones de aplicación (size, pages, iters, threads, seed, file, verify, nick, duration) | `dsm_app_run` |

## 12.2 Funciones públicas

| Función | Firma resumida | Descripción |
|---|---|---|
| `dsm_malloc` | `void *dsm_malloc(rt, size)` | Reserva un objeto compartido; devuelve puntero a la ventana local. |
| `dsm_free` | `int dsm_free(rt, ptr)` | Libera un objeto (`PAGE_RELEASE` al home). |
| `dsm_barrier` | `dsm_status_t dsm_barrier(rt, timeout_ms)` | Barrera centralizada con timeout. |
| `dsm_stats` | `dsm_status_t dsm_stats(rt, view)` | Snapshot de contadores. |
| `dsm_attach` | `dsm_status_t dsm_attach(rt)` | Registra el hilo actual para fallos de página. |
| `dsm_detach` | `void dsm_detach(rt)` | Desregistra el hilo. |
| `dsm_node_id` | `uint32_t dsm_node_id(rt)` | Id del nodo. |
| `dsm_node_count` | `uint32_t dsm_node_count(rt)` | Nodos en la tabla. |
| `dsm_page_size` | `uint32_t dsm_page_size(rt)` | Tamaño de página (4096). |

## 12.3 Parámetros y errores

- `dsm_malloc` retorna `NULL` si no hay memoria o el home no responde.
- `dsm_barrier` retorna `DSM_ERR_TIMEOUT` si no todos los nodos llegan a tiempo.
- `dsm_stats` refresca contadores derivados (latencia p50/p95, throughput).
- Los errores se propagan como `dsm_status_t` y se registran en el log.

## 12.4 Aplicaciones

El dispatcher `dsm_app_run(rt, kind, opts)` ejecuta una de las aplicaciones: `demo`, `matrices`, `file`, `counter`, `bench`, `evict`, `chat`. Cada una corre en todos los nodos (modelo SPMD) y usa `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_stats`.

---

*Fin de la sección 12.*
