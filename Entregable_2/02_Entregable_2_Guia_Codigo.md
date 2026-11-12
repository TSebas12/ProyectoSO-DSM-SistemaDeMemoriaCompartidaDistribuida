# Guía del código — Prototipo 1

**Proyecto:** Sistema de Memoria Compartida Distribuida (DSM)
**Curso:** IC-6600 Principios de Sistemas Operativos
**Entregable:** 2 — Prototipo 1
**Ubicación del código:** `Entregable_2/prototipo1/`

---

## 1. Alcance de este documento

Esta guía explica **qué hace cada archivo y por qué**. Cubre los módulos de Sebastián Agüero Brenes:

- tipos base y códigos de estado
- sincronización y jerarquía de locks
- contadores
- memoria virtual y tabla de páginas
- señales y fallos de página
- coherencia MSI y directorio

La sección 6 define el contrato entre coherencia y red, que es la frontera que más problemas dio al integrar. La sección 8 registra el estado real de la integración: qué quedó implementado por José Daniel Monterrosa (`config`, `protocol`, `net`, `log`, `node_main`, `ctl_main`, `scripts`) y qué se corrigió al unirlo.

> **Regla de lectura.** Cada elemento se explica en dos a cuatro líneas. Si algo necesita más, es señal de que el archivo hace demasiado.

---

## 2. Mapa de archivos

| Archivo | Responsabilidad | Líneas |
|---|---|---:|
| `include/dsm_types.h` | Enums, códigos de estado, constantes y `dsm_msg_t` | 195 |
| `include/dsm_sync.h` + `src/sync.c` | Envoltorios de Pthreads, semáforos y orden de locks | 83 + 310 |
| `include/dsm_stats.h` + `src/stats.c` | Contadores atómicos de 128 B | 80 + 145 |
| `include/dsm_memory.h` + `src/memory.c` | Ventana, tabla de páginas, marcos y `mprotect` | 142 + 492 |
| `include/dsm_fault.h` + `src/fault.c` | `sigaction`, `sigaltstack`, cola sin locks | 75 + 453 |
| `include/dsm_coherence.h` + `src/coherence.c` | MSI, directorio y resolución de fallos | 117 + 617 |
| `include/dsm_config.h` + `src/config.c` | Parser plano `clave = valor` y validación | 53 + 113 |
| `include/dsm_protocol.h` + `src/protocol.c` | Cabecera de 64 B y códec | 30 + 92 |
| `include/dsm_net.h` + `src/net.c` | Sockets, RPC y contadores de tráfico | 28 + 334 |
| `include/dsm_log.h` + `src/log.c` | Registro con nivel y destino | 9 + 34 |
| `include/dsm_api.h` + `src/dsm_api.c` | `dsm_init`, `dsm_shutdown`, `dsm_create`, `dsm_attach` | 10 + 73 |
| `include/dsm_runtime.h` + `src/runtime.c` | Estado del nodo, hilos y parada ordenada | 33 + 77 |
| `src/node_main.c` | Binario `dsm_node` | 58 |
| `src/ctl_main.c` | Binario `dsmctl` (stub) | 6 |
| `src/demo_page.c` | Demo de dos nodos sobre la página 0 | 153 |
| `Makefile` | Objetos en `build/`, enlaza `dsm_node` y `dsmctl` | 29 |
| `config/dsm.conf`, `config/dsm-node1.conf` | Configuración de los dos nodos | 34 + 34 |
| `scripts/*.sh` | Compilar, arrancar, demostrar y capturar evidencia | — |

**Total:** 3 812 líneas de C (12 cabeceras y 14 fuentes) más 29 del `Makefile`.

**Orden de dependencias.** `types` → `sync` → `stats` → `memory` → `fault` → `coherence`. Ningún archivo de esa cadena incluye uno que esté por encima suyo, así que no hay ciclos. La red y la configuración dependen de `types`; el runtime depende de todos.

---

## 3. Cómo se verifica

El proyecto se compiló y se ejecutó en Linux (Alpine 3.20.3 sobre WSL2, GCC 13.2.1) y los 14 archivos se compilaron además con clang contra cabeceras de glibc. **También se compiló y ejecutó en las dos máquinas del laboratorio** (Ubuntu 24.04, kernel 6.8.0-138-generic, GCC 13.3.0).

```bash
cd Entregable_2/prototipo1
make              # objetos + dsm_node + dsmctl
sh scripts/demo.sh                 # demo de dos nodos
sh scripts/check_ports.sh          # puerto 27300 en escucha
sh scripts/capture_evidence.sh     # regenera evidence/
make clean
```

Resultado: **0 errores y 0 advertencias** con `-Wall -Wextra -Wpedantic` tanto en WSL2 (musl) como en el laboratorio (glibc). En GCC 13.3.0 se añadió precisión literal en `config.c` (líneas 27, 33, 50: `%.63s`, `%.255s`, `%.255s`) para evitar `-Wformat-truncation`. Los `_Static_assert` de tamaño pasan en cada compilación. La evidencia de la corrida está en `evidence/`.

> **Antes de integrar** solo existía una validación sintáctica con `pycparser`; esa etapa quedó atrás y no se usa como prueba de compilación.

---

## 4. Módulo por módulo

### 4.1 `include/dsm_types.h`

**Qué hace.** Define los enums del protocolo (estado MSI, residencia, tipo de fallo, evento, tipo de mensaje), los 21 códigos de estado del API y las constantes del sistema.

| Elemento | Qué hace | Por qué |
|---|---|---|
| `dsm_status_t` | Códigos `DSM_OK` a `DSM_ERR_SHUTDOWN` | Un solo vocabulario de error para todo el runtime |
| `dsm_msi_state_t` | `DSM_PAGE_I`, `DSM_PAGE_S`, `DSM_PAGE_M` | Los valores viajan por la red; no deben cambiar |
| `dsm_msg_t` | Mensaje lógico entre coherencia y red | La coherencia no debe saber de sockets ni de cabeceras de 64 B |
| `dsm_msi_state_name()` | Devuelve `"I"`, `"S"` o `"M"` | Evitar `switch` repetidos en los logs |

**Depende de:** nada del proyecto.
**No hace:** no reserva memoria ni abre archivos.

---

### 4.2 `include/dsm_sync.h` + `src/sync.c`

**Qué hace.** Envuelve mutex, variables de condición y semáforos POSIX, y verifica que los locks se tomen en el orden correcto.

| Función | Qué hace | Por qué |
|---|---|---|
| `dsm_mutex_lock()` | Toma el mutex y registra su clase | La clase es lo que permite detectar el orden invertido |
| `check_order()` | Compara la clase nueva con la que ya estaba tomada | Un `GLOBAL` después de un `PAGINA` es un interbloqueo esperando ocurrir |
| `dsm_cond_wait()` | Espera y descuenta la clase mientras el mutex está suelto | Mientras espera, el hilo no tiene el lock y el orden no debe contarse |
| `dsm_sem_timedwait()` | Espera con plazo y devuelve `DSM_ERR_TIMEOUT` | Los RPC de página necesitan rendirse en algún momento |
| `dsm_now_ms()` | Milisegundos de `CLOCK_MONOTONIC` | Comparar tiempos sin sufrir saltos del reloj de pared |

**Jerarquía.** `GLOBAL` (0) → `OBJECT` (1) → `PAGE` (2) → `FRAME` (3). Siempre en orden creciente.

**Depende de:** `dsm_types.h`.
**No hace:** no crea hilos ni administra la cola de fallos.

---

### 4.3 `include/dsm_stats.h` + `src/stats.c`

**Qué hace.** Mantiene los 16 contadores del nodo en un bloque de 128 bytes, con operaciones atómicas.

| Función | Qué hace | Por qué |
|---|---|---|
| `dsm_stats_inc()` | Suma 1 a un campo | Lo llaman hilos distintos sin ningún lock |
| `dsm_stats_snapshot()` | Copia los 16 valores a una vista plana | Imprimir o enviar sin mantener el bloque vivo |
| `dsm_stats_format()` | Arma el resumen legible | Lo usan los logs y `dsmctl status` |

**Truco del diseño.** Una unión anónima permite ver el mismo bloque por nombre (`s->page_faults`) o por índice (`s->field[DSM_STAT_PAGE_FAULTS]`). Así no hace falta repetir un `switch` de 16 casos en cada función.

**Depende de:** nada del proyecto.
**No hace:** no mide tiempos; para eso está `dsm_now_ms()`.

---

### 4.4 `include/dsm_memory.h` + `src/memory.c`

**Qué hace.** Reserva la ventana de direcciones, mantiene la tabla de páginas, asigna marcos y aplica los permisos de cada página.

| Función | Qué hace | Por qué |
|---|---|---|
| `dsm_memory_init()` | `mmap` de la ventana en `PROT_NONE` | Reservar direcciones no compromete memoria física |
| `dsm_memory_addr_to_page()` | Traduce una dirección a `page_id` | Es la misma cuenta que hace el manejador de señales |
| `dsm_memory_page_get_or_create()` | Busca o crea la entrada del hash | Solo existen entradas para páginas tocadas |
| `dsm_memory_protect()` | `mprotect` según el estado MSI | Es lo que provoca el siguiente fallo cuando corresponde |
| `dsm_memory_copy_in/out()` | Copian datos por el mapeo de respaldo | La ventana sigue en `PROT_NONE` mientras se copia: nadie ve datos a medias |
| `dsm_memory_alloc_frame()` | Reserva un slot del pool | Limita cuántas páginas pueden estar residentes |

**Estructura.** `dsm_local_page_t` conserva los 56 bytes del E1. Los objetos de sincronización van en `dsm_page_entry_t`, junto a los metadatos, para no romper el tamaño documentado.

**Doble mapeo.** La ventana se crea sobre un `memfd` que se mapea dos veces: una en `PROT_NONE` (la que ven los hilos de usuario) y otra en `RW` (la que usa el resolutor para copiar). Son las mismas páginas físicas, así que el resolutor escribe por el respaldo y solo después publica el permiso en la ventana. Nadie puede observar el estado intermedio.

**Requisito.** `memfd_create` existe desde Linux 3.17 y glibc 2.27. Cualquier Ubuntu 20.04 o posterior lo tiene; si el laboratorio usara una versión anterior, hay que sustituirlo por `shm_open` + `shm_unlink` en `dsm_memory_init()`.

**Depende de:** `dsm_types.h`, `dsm_sync.h`.
**No hace:** no decide estados MSI ni habla con la red; solo obedece a la capa de coherencia.

---

### 4.5 `include/dsm_fault.h` + `src/fault.c`

**Qué hace.** Instala el manejador de `SIGSEGV` con pila alternativa, encola el fallo y duerme al hilo hasta que el resolutor lo despierte.

| Función | Qué hace | Por qué |
|---|---|---|
| `dsm_fault_install()` | `sigaltstack` + `sigaction` para `SIGSEGV` y `SIGUSR1` | El manejador necesita pila propia para no desbordar la del hilo |
| `dsm_segv_handler()` | Calcula `page_id`, encola y hace `sigsuspend` | Es lo único que se puede hacer con seguridad dentro de una señal |
| `queue_push()` | Anillo sin locks con atómicos de C11 | Un mutex aquí puede interbloquear si el hilo lo tenía tomado |
| `dsm_fault_queue_pop()` | Saca un fallo con plazo opcional | Lo consume el hilo resolutor |
| `dsm_fault_wake()` | Baja `in_fault` y manda `SIGUSR1` al hilo | `pthread_kill` es segura para señales; `sem_post` al hilo no serviría |
| `dsm_fault_mark_failed()` | Marca el contexto como fallido | Evita que el reintento entre en un bucle infinito |

**Qué no hace el manejador.** No hace `malloc`, ni E/S, ni logs, ni toma locks, ni toca la red. Si la dirección no pertenece a la ventana, restaura el manejador anterior y relanza la señal.

**Pila alternativa por hilo.** `sigaltstack` es por hilo, no por proceso. Si todos los hilos compartieran un buffer, dos fallos simultáneos escribirían la misma pila. Por eso `dsm_fault_attach_thread()` reserva una pila propia para cada hilo y la libera con un destructor de `pthread_key`.

**Depende de:** `dsm_types.h`, `dsm_sync.h`.
**No hace:** no resuelve la página; solo la pide.

---

### 4.6 `include/dsm_coherence.h` + `src/coherence.c`

**Qué hace.** Implementa MSI: resuelve fallos trayendo páginas, atiende las peticiones del nodo home y mantiene el directorio.

| Función | Qué hace | Por qué |
|---|---|---|
| `dsm_msi_next_state()` | Tabla de transiciones pura | Se puede probar sin red ni hilos |
| `dsm_coherence_handle_fault()` | Resuelve un fallo de página | Es el punto de entrada del hilo resolutor |
| `coherence_fetch()` | Hace el RPC y publica los datos | Se ejecuta **sin** el lock de la página |
| `serve_page_request()` | Entrega una copia en `S` | Lado home de `PAGE_REQUEST` |
| `serve_ownership_request()` | Concede la propiedad | Lado home de `OWNERSHIP_REQUEST` |
| `handle_invalidate()` | Pasa a `I` y confirma | Lado cliente de `INVALIDATE` |
| `dir_mark_sharer/owner()` | Actualiza el bitmap del directorio | El home necesita saber quién tiene copia |

**Regla que ordena el archivo.** Nunca se espera a la red con el lock de una página tomado. Se marca `TRAYENDO`, se suelta el lock, se hace el RPC y se vuelve a tomar para publicar.

**Coalescencia.** Si dos hilos fallan en la misma página, el segundo espera en la variable de condición en lugar de lanzar un segundo RPC.

**Depende de:** `dsm_types.h`, `dsm_memory.h`, `dsm_stats.h`, `dsm_fault.h`.
**No hace:** no abre sockets ni serializa bytes; todo eso pasa por `dsm_coherence_ops_t`.

---

## 5. Flujos completos

### 5.1 Arranque del nodo

```text
main (node_main.c)
  → dsm_config_load()          lee dsm.conf y valida
  → dsm_init()                 reserva runtime
      → dsm_memory_init()      reserva la ventana
      → dsm_stats_init()       contadores en cero
      → dsm_fault_install()    instala SIGSEGV con la ventana
      → dsm_net_create()       sockets y callbacks de red
      → dsm_coherence_init()   directorio y estados MSI
      → dsm_net_start()        hilo de escucha
      → pthread_create()       hilo resolutor
  → dsm_create()               solo el home: 4 páginas residentes en M
  → dsm_attach()               registra el hilo actual en el manejador
  → dsm_demo_page_run()        o bucle de servidor con --serve
```

Cada hilo que vaya a tocar la ventana debe llamar antes a `dsm_fault_attach_thread(&ctx)`. En el prototipo 1 solo lo hace el hilo principal, a través de `dsm_attach()`.

### 5.2 Fallo de lectura

```text
hilo: lee la ventana → SIGSEGV (PROT_NONE)
  handler: page_id, kind = LECTURA, encola, sigsuspend
resolutor: dsm_coherence_handle_fault()
  página I → marca TRAYENDO → suelta lock
  fetch_shared() → PAGE_REQUEST al home → PAGE_DATA
  copy_in() + mprotect(READ) → estado S
  despierta a los que esperaban
  dsm_fault_wake(ctx) → SIGUSR1
hilo: vuelve del handler, el kernel reintenta la instrucción
```

### 5.3 Fallo de escritura

Igual que el anterior, pero `kind = ESCRITURA`. Si la página ya estaba en `S`, se pide la propiedad con `fetch_exclusive()` y se pasa a `M` con `PROT_READ|PROT_WRITE`.

### 5.4 Invalidación

```text
home: recibe OWNERSHIP_REQUEST de otro nodo
  entrega los datos con OWNERSHIP_GRANT
  su propia copia pasa a I y libera el marco
cliente: recibe INVALIDATE
  pasa a I, PROT_NONE, libera el marco
  responde INVALIDATE_ACK
```

---

## 6. Contrato con la capa de red

Esta sección documenta la frontera entre coherencia y red: lo que la coherencia espera de `dsm_coherence_ops_t` y lo que la red debe entregarle. Es el contrato que permitió integrar ambos trabajos sin cambiar firmas.

### 6.1 Lo que debe proveer

```c
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
```

| Callback | Qué debe hacer | Detalle que no se puede olvidar |
|---|---|---|
| `send` | Serializar `dsm_msg_t` y enviarlo | **No** puede guardar los punteros `payload` ni `data`: el llamador los libera al volver |
| `fetch_shared` | Enviar `PAGE_REQUEST`, esperar `PAGE_DATA`, copiar a `buf` | Es bloqueante y debe respetar `timeout_ms` |
| `fetch_exclusive` | Enviar `OWNERSHIP_REQUEST`, esperar `OWNERSHIP_GRANT` | Igual de bloqueante |
| `writeback` | Enviar `PAGE_WRITEBACK` y esperar `PAGE_WRITEBACK_ACK` | Lo usa el nodo que cede una página sucia; en la demo lo hace el nodo 1 al terminar |

### 6.2 Lo que debe consumir

Los mensajes que llegan del home se pasan tal cual a:

```c
dsm_coherence_handle_message(&co, &msg);
```

Solo se esperan cuatro tipos: `PAGE_REQUEST`, `OWNERSHIP_REQUEST`, `INVALIDATE` y `PAGE_WRITEBACK`. Las respuestas (`PAGE_DATA`, `OWNERSHIP_GRANT`, `INVALIDATE_ACK`) las consume la propia capa de red dentro del RPC y **no** deben llegar a esa función.

### 6.3 El bucle del resolutor

Va en `runtime.c`. Son seis líneas y no requiere tocar nada de Sebastián:

```c
for (;;) {
    dsm_fault_req_t req;

    if (dsm_fault_queue_pop(&req, UINT32_MAX) != DSM_OK) {
        break;                      /* DSM_ERR_SHUTDOWN o error */
    }
    if (dsm_coherence_handle_fault(&co, &req) != DSM_OK) {
        dsm_fault_mark_failed(req.ctx);
    }
    dsm_fault_wake(req.ctx);        /* siempre: éxito o fallo */
}
```

Al cerrar el nodo, `runtime.c` llama a `dsm_fault_shutdown()` para que el bucle salga.

### 6.4 Qué no debe cambiar

| Elemento | Motivo |
|---|---|
| Firmas de `dsm_memory_*`, `dsm_fault_*`, `dsm_coherence_*` | Ya están usadas entre módulos y documentadas en el E2 |
| `dsm_msg_t` y los códigos de `dsm_msg_type_t` | Son el punto de unión; cambiarlos obliga a tocar los dos lados |
| Jerarquía `GLOBAL → OBJECT → PAGE → FRAME` | Si la red introduce un lock nuevo, debe encajar en esa escala |

Si hace falta cambiar algo de esta lista, se avisa y se anota en la sección 5.4 del documento del entregable. **Editar un archivo de Sebastián para completar la integración está permitido**: solo hay que registrar el cambio.

---

## 7. Desviaciones respecto al Entregable 1

Estas filas alimentan directamente las secciones 5.4 y 8.3 del documento del E2.

| Elemento del E1 | Qué decía el E1 | Qué hace el prototipo | Motivo |
|---|---|---|---|
| Mecanismo de copia | `PROT_NONE` → copiar → publicar permisos, sin decir cómo | Mismo orden, pero la copia va por un segundo mapeo RW del mismo `memfd` | Sin ese mapeo habría que abrir la ventana en RW antes de copiar y otro hilo podría leer datos a medias |
| Pool de marcos | Pool de 1 MB separado de la ventana | Tabla de marcos; los datos viven en la ventana | Sin expulsión en el E2, duplicar los datos no aporta nada |
| `dsm_local_page_t` | 56 B con todos los metadatos | 56 B de metadatos + sincronización en `dsm_page_entry_t` | Conservar el tamaño documentado y aun así tener mutex por página |
| Directorio | Distribuido por página con replicación | Un directorio local en el home con bitmap de sharers | Dos nodos y cuatro páginas |
| Invalidación de terceros | `INVALIDATE` a todos y esperar ACK | Si hay un tercero, se responde `OWNERSHIP_DENY` | Con dos nodos no existen terceros; queda para el E3 |
| Detección lectura/escritura | x86-64 y ARM64 | x86-64; otras arquitecturas asumen escritura | El laboratorio es x86-64 |
| `dsm_thread_ctx_t` | 5 campos | 5 campos + `failed` atómico | Poder abortar en vez de repetir el fallo para siempre |
| Buffer de página | No se especificaba | `malloc` por operación | Simplicidad; en el E3 se puede pasar a un pool |

---

## 8. Estado de la integración

Los módulos de José Daniel Monterrosa están implementados y unidos a los de Sebastián. Al ejecutar la demo aparecieron nueve defectos de frontera; se corrigieron y quedaron documentados en la tabla 8 del entregable.

| Módulo | Estado | Observación |
|---|---|---|
| `config.c/h` | Implementado | Parser plano, valores por defecto, validación y guardado |
| `protocol.c/h` | Implementado | Cabecera de 64 B en orden de red; `checksum` viaja en cero |
| `net.c/h` | Implementado | Una conexión por RPC; respuesta por el socket de la petición; contadores enganchados |
| `log.c/h` | Implementado | Nivel y destino desde la configuración |
| `node_main.c` | Implementado | `--config`, `--serve`, `--check-config`, `--version` |
| `ctl_main.c` | Stub | Imprime `dsmctl: not fully implemented` |
| `runtime.c` | Implementado | Orden de arranque y parada; contexto del hilo de la app |
| `dsm_api.c` | Implementado | `dsm_init`, `dsm_shutdown`, `dsm_create`, `dsm_attach` |
| `demo_page.c` | Implementado | Home crea y espera; cliente lee, escribe y devuelve |
| `tests/` | Pendiente | Llegan con el prototipo 2 |

**Qué se corrigió al integrar.** El home respondía por una conexión nueva en lugar de por el socket del RPC; `dsm_attach` y `dsm_create` eran stubs; el parser desbordaba la pila con `strcpy`; `dsm_net_stop` se colgaba en `accept()`; el indicador de ejecución no era atómico; el home podía pedirse una página a sí mismo; el directorio no registraba al *sharer* tras el writeback; y la red no reportaba contadores. El detalle está en `02_Entregable_2_Prototipo_1.md`, sección 10.4.

**Cómo se reproduce la demo.**

```bash
make
sh scripts/demo.sh          # nodo 0 + nodo 1, imprime las dos salidas
```

Nodo 0 crea las 4 páginas y espera. Nodo 1 lee la página 0 (`I → S`), la escribe (`S → M`), la relee sin fallo y la devuelve con `WRITEBACK`. Nodo 0 detecta la versión nueva y lee el texto del nodo 1.

---

## 9. Resumen

- Doce cabeceras y catorce fuentes compilan y enlazan sin advertencias (GCC 13.2.1 musl, GCC 13.3.0 glibc, clang 22.1.8 glibc).
- La coherencia no conoce sockets: se une por `dsm_coherence_ops_t`.
- La jerarquía de locks se verifica en tiempo de ejecución.
- El manejador de señales no toma locks ni reserva memoria.
- La demo de dos nodos se ejecutó en WSL2 y en las dos máquinas físicas del laboratorio (`192.0.2.10` ↔ `192.0.2.11`); su salida está en `evidence/`.
- Lo que no está implementado se declara: `dsmctl`, expulsión, terceros, métricas y pruebas unitarias.
