# Plan de trabajo — Entrega Final

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Hito** | Entrega Final — Demo en vivo en el laboratorio |
| **Fecha oficial** | Viernes 27 de noviembre de 2026 |
| **Documento** | `Entrega_Final/PLAN_TRABAJO.md` |
| **Versión** | 1.0 — 20 de noviembre de 2026 |
| **Estado** | Propuesta de división; pendiente de confirmación de ambos integrantes |
| **Integrantes** | Sebastián Agüero Brenes (Parte A) · José Daniel Monterrosa Quirós (Parte B) |

---

## Control de versiones

| Versión | Fecha | Cambio |
|---|---|---|
| 1.0 | 20 nov 2026 | División inicial del trabajo, alcance, calendario, extras y criterios de aceptación. |

> **Alcance de este documento.** Es un plan de trabajo, no código. Define quién hace qué, en qué archivos, con qué criterio de aceptación y en qué orden. Cualquier cambio de alcance se anota acá antes de ejecutarse.

---

## Índice

1. [Resumen de la división](#1-resumen-de-la-división)
2. [Inventario: qué pide la Entrega Final y qué falta](#2-inventario-qué-pide-la-entrega-final-y-qué-falta)
3. [Reglas de trabajo](#3-reglas-de-trabajo)
4. [Parte A — Sebastián](#4-parte-a--sebastián)
5. [Parte B — Daniel](#5-parte-b--daniel)
6. [Trabajo compartido](#6-trabajo-compartido)
7. [Calendario 21–27 de noviembre](#7-calendario-2127-de-noviembre)
8. [Balance de horas](#8-balance-de-horas)
9. [Extras](#9-extras)
10. [Los tres puntos clave de la demo](#10-los-tres-puntos-clave-de-la-demo)
11. [Riesgos y plan B](#11-riesgos-y-plan-b)
12. [Definición de terminado y checklist de cierre](#12-definición-de-terminado-y-checklist-de-cierre)
13. [Próximos pasos](#13-próximos-pasos)
14. [Anexos: convenciones y comandos](#14-anexos-convenciones-y-comandos)

---

## 1. Resumen de la división

| Parte | Responsable | Enfoque | Horas individuales |
|---|---|---|---|
| **A** | **Sebastián Agüero** | Núcleo DSM, coherencia, configuración multi-nodo, informe técnico, integración/PDF, demo técnica | **~34 h** |
| **B** | **Daniel Monterrosa** | App de chat, correcciones de apps, `dsm.sh` 3–4 PCs, pruebas de laboratorio, evidencia, manual, demo operativa | **~36 h** |
| **Compartido** | Ambos | Revisión cruzada, ensayos, defensa individual, cierre | **~6 h c/u** |

**Total: ~40 h por persona en 6 días (21–26 nov), con demo el 27.**  
Si el tiempo no alcanza, se recortan primero los extras; nunca la base obligatoria.

**Regla de oro:** la base (compila, 2 PCs, 4 PCs, chat, informe, manual, demo) se cierra antes de tocar extras.

---

## 2. Inventario: qué pide la Entrega Final y qué falta

### 2.1 Requisitos obligatorios

| Requisito del PDF / instrucciones | Estado actual | Brecha | Dueño |
|---|---|---|---|
| Código fuente final documentado y modular | 25 módulos, 18 cabeceras | `counter` 2000×4, `evict` 4n/512p, `local_drop`, `dsm.sh` solo A/B | A + B |
| `Makefile` | Existe | Falta `app_chat.c`; falta `make clean` probado | B |
| Guía de configuración, instalación y ejecución | Manual E3 + README | Falta 3–4 PCs, chat, cambio de config, web | B |
| Enlace al repositorio Git | ✅ | — | — |
| Commits de cada integrante durante las semanas | ⚠️ Sebastián 18, Daniel 2 | Daniel necesita commits propios y frecuentes | B |
| Aplicaciones cliente reales | 6 apps | Falta texto libre interactivo (`chat`) y datos reales propios | B |
| Prueba de estrés / datos reales / salidas esperadas | 2 PCs + 4 loopback | Falta 3 PCs, 4 PCs, comparativa 2 vs 4, datos reales | B |
| Informe técnico completo (21 secciones) | No existe | Redactar y ensamblar | A + B |
| Manual de usuario | E3 | Actualizar a versión final | B |
| Demo en vivo | No ensayada | Guion, ensayos, respaldo | A + B |
| Defensa individual | No preparada | Preguntas y entrenamiento cruzado | A + B |
| Evidencia | Capturas pendientes; web sin verificar | Logs, capturas, web, Git | B |
| Configuración de ejemplo | 4 nodos loopback + 2 PCs | `nodes_1/2/3/4.conf` y roles C/D | B |
| `README` final | E3 | Actualizar estado final | A |
| 4 PCs confirmadas | ⚠️ D-009 parcial | Confirmar con laboratorio | A |
| Panel web | Implementado, no verificado | Probar con `curl`/navegador | B |

### 2.2 Requisitos de la demo

| Requisito | Estado | Acción |
|---|---|---|
| Demo en laboratorio (Lab H o similar) | ⏳ | Confirmar hora y laboratorio |
| 1 a 2 horas por pareja | ⏳ | Guion de 110 min + 10 de cierre |
| Ambos integrantes presentes | ⏳ | Confirmar disponibilidad |
| Ambos explican todas las partes | ⏳ | Entrenamiento cruzado |
| Nota individual | — | Preparar defensa por separado |
| Sistema compila y ejecuta en el laboratorio | ⚠️ | Prueba en 3 y 4 PCs |
| Al menos dos aplicaciones reales | ✅ | `matrices`, `file`, `chat` |
| Datos sincronizados y verificables | ✅ | Checksums y verificación |
| Coherencia MSI demostrable | ✅ | `demo`, `chat`, `counter` |
| Documentación completa | ❌ | Informe final + manual |
| Repositorio con commits de ambos | ⚠️ | Plan de commits de Daniel |

### 2.3 Estructura obligatoria del informe final (21 secciones)

| # | Sección | Dueño | Fuente base |
|---|---|---|---|
| 1 | Portada | Sebastián | Nueva |
| 2 | Resumen ejecutivo | Ambos (Sebastián lead) | Nueva |
| 3 | Índice | Generado | — |
| 4 | Introducción (problema, objetivos, alcance) | Daniel | E1/E3 |
| 5 | Marco teórico (DSM, paginación, MSI, señales, sockets, Pthreads) | Daniel | E1 |
| 6 | Arquitectura final | Sebastián | E1/E3 + cambios |
| 7 | Módulos implementados | Sebastián | Guía de código |
| 8 | Diagrama de estados MSI | Sebastián | E1/E3 |
| 9 | Concurrencia entre señales y sockets | Sebastián | E1/E3 |
| 10 | Interbloqueos y su resolución | Sebastián | E1/E3 + fix |
| 11 | Estructuras de datos y archivos | Sebastián | E1/guía |
| 12 | API implementado | Sebastián | E1/guía |
| 13 | Protocolo de mensajería | Sebastián | E1/guía + huella |
| 14 | Aplicaciones de prueba | Daniel | E3 + chat |
| 15 | Pruebas de uso y ejecución | Daniel | Nuevas corridas |
| 16 | Rendimiento | Daniel | Nuevas métricas |
| 17 | Extras | Daniel + Sebastián | Nuevo |
| 18 | Manual de usuario | Daniel | Manual E3 |
| 19 | Conclusiones | Ambos (Sebastián lead) | Nuevo |
| 20 | Referencias | Daniel | E3 |
| 21 | Anexos (logs, capturas, tablas, gráficos, Git) | Daniel + Sebastián | Nuevo |

---

## 3. Reglas de trabajo

1. **Un dueño por archivo.** Nadie edita el archivo del otro sin avisar. Los archivos compartidos se tocan solo en un PR revisado.
2. **Cada quien commitea lo suyo.** Daniel no debe tener commits hechos por Sebastián (ni `--author`). Sebastián revisa y hace merge.
3. **Ramas y PRs.**
   - Daniel: `daniel/final-chat`, `daniel/final-dsm-sh`, `daniel/final-docs`.
   - Sebastián: `sebas/final-core`, `sebas/final-informe`, `sebas/final-pdf`.
   - `main` se actualiza con PR revisado. Esto deja evidencia real de colaboración.
4. **Definición de “terminado”:** compila con `make` sin warnings; corre en 2 PCs; la funcionalidad tiene prueba reproducible; la documentación quedó actualizada; el commit es del autor.
5. **No inventar datos ni capturas.** Si una prueba falla, se documenta el fallo real y el plan B.
6. **Datos sensibles:**
   - IPs de ejemplo `192.0.2.10–13` en documentación.
   - IPs reales solo en `config/` local, nunca en commits.
   - PDF público sin carné, profesor, sede ni institución.
   - Revisar con `git grep` antes de cada push.
7. **Sincronización diaria:** 15 minutos por la noche para revisar bloqueos, horas y qué se mergea.
8. **Congelamiento:** código cerrado el **jueves 26 a las 18:00**. Después solo se corrige lo que rompa la demo.
9. **Idioma:** comentarios y documentación en español; identificadores de código en inglés `snake_case`.
10. **Formato:** Markdown académico, tablas, diagramas Mermaid, referencias y fragmentos de código cuando aporten.

---

## 4. Parte A — Sebastián

### A0. Coordinación y alcance — 2 h

- [ ] Confirmar **4 PCs** y la cita del 27 con el laboratorio (Lab H o similar). Si no hay 4, dejar 3 + 4 loopback como respaldo.
- [ ] Confirmar con Daniel que acepta la Parte B (o intercambiar un bloque si prefiere).
- [ ] Decidir extras según el gate del miércoles (ver §9).
- [ ] Crear `Entrega_Final/secciones/` con un archivo por sección para evitar conflictos de Markdown.
- [ ] Registrar decisiones nuevas:
  - `D-020`: la configuración no tiene recarga en caliente; se aplica reiniciando el clúster.
  - `D-021`: topología oficial 2/3/4 PCs con tablas `nodes_2/3/4.conf`.
- [ ] Mantener este plan actualizado si cambia algo.

**Entregable:** este documento actualizado y un acta corta de acuerdos.

---

### A1. Núcleo de coherencia y memoria — 9 h

#### A1.1 `counter 2000×4` pierde incrementos — 3 h

- **Reproducir:** 2 PCs o loopback con `--app counter --iters 2000 --threads 4`; hoy da `8000/16000`.
- **Arreglar en `src/coherence.c`:** revisar la cesión de propiedad del home.
  - Antes de soltar la copia `M`, copiar el contenido sucio al nuevo dueño/home y esperar ACK.
  - No soltar mientras haya escrituras locales en vuelo.
  - Documentar el invariante en el comentario del código.
- **Probar:** 3 corridas seguidas → `suma = 16000, esperado = 16000, OK`.
- **Sin tocar `app_counter.c`** (eso es de Daniel, B2.2); si el arreglo de app alcanza, igual dejar el arreglo de núcleo porque afecta a otras apps.
- **Documentar:** §11 y §12 del informe final.

#### A1.2 `local_drop` con `op_lock` tomado — 2 h

- **Archivo:** `src/coherence.c` (`local_drop` ~478, `local_fetch` ~1009, `op_lock`).
- **Problema:** `local_drop` espera hasta 200 ms con `op_lock`; `local_fetch` necesita ese lock. Es una espera con lock global → riesgo de bloqueo/latencia.
- **Arreglo:**
  - Soltar `op_lock` antes de esperar.
  - Revalidar al volver.
  - Usar timeout y log.
  - Regla: nunca esperar red/peer con `op_lock`.
- **Probar:** 4 nodos, `evict` y `matrices` durante 30 minutos; sin cuelgues.

#### A1.3 `evict` 4 nodos / 512 páginas — 2 h

- **Reproducir:** `--app evict --pages 512 --frames 32` con 4 nodos.
- **Mitigar:** backlog de `accept`, reintento de `release_to_home`, límite de ráfagas concurrentes o backoff.
- **Si no queda estable:** no bloquear la demo. Dejar `64/32` como configuración oficial y documentar el caso grande como estrés opcional.
- **Criterio:** 3 corridas seguidas con `tiempos agotados=0`, o fallo rápido documentado sin corrupción.

#### A1.4 Revisión de los fixes de Daniel — 2 h

- Revisar `\n` literales, `dsm.sh` multi-nodo y `app_chat.c`.
- Compilar y correr 2 PCs.
- Revisar que no se rompa el protocolo ni la coherencia.
- Mergear PRs con revisión.

---

### A2. Configuración multi-nodo y huella de configuración — 5 h

#### A2.1 Función de huella — 2 h

- **Archivos:** `include/dsm_config.h`, `src/config.c`, `src/runtime.c`.
- Agregar `uint32_t dsm_config_fingerprint(const dsm_config_t *cfg, const dsm_nodes_t *nodes)`.
- Calcular FNV-1a sobre:
  - `page_size`
  - `node_memory`
  - `max_nodes`
  - versión de protocolo
  - tabla de nodos ordenada (`id`, `host`, `port`)
- **No** incluir `node_id` ni `log_file`.
- Guardar `cluster_hash` en el runtime al arrancar.

#### A2.2 Verificación en el protocolo — 2 h

- **Archivos:** `include/dsm_protocol.h` (usar `reserved2` de la cabecera de 64 B), `src/net.c`, `src/protocol.c`, `include/dsm_types.h`.
- Poner la huella en `reserved2` de cada mensaje saliente y compararla al recibir.
- Si no coincide:
  - `DSM_ERR_CONFIG_MISMATCH`.
  - Log claro `config mismatch: local=%08x remoto=%08x`.
  - Fallo rápido, sin esperar 10 s a un timeout.
- **No cambiar el tamaño de la cabecera.** Es compatible porque usa un campo reservado.
- **Prueba:** mismo `page_size` → mismo hash; cambiar `page_size` en un nodo → error explícito, no timeout.

#### A2.3 `dsmctl config` — 1 h

- **Archivos:** `src/control.c`, `src/ctl_main.c` si hace falta.
- Nuevo comando `config`: imprime `node_id`, `cluster_hash`, tabla de nodos y parámetros clave (`page_size`, `node_memory`, `max_nodes`, `replication_factor`).
- Sirve para que en la demo se compare la huella en las 4 PCs.

---

### A3. Informe técnico: §6–§13 y §19 — 10 h

**Regla:** reutilizar E1/E3 y la guía de código; no reescribir desde cero. Cada sección va en `Entrega_Final/secciones/NN_nombre.md`.

| § | Sección | Fuente base | Horas |
|---|---|---|---|
| 6 | Arquitectura final (componentes, despliegue, modelo de memoria, administración distribuida, heartbeat) | E1/E3 + 4 nodos + chat + huella | 1.5 |
| 7 | Módulos implementados (memoria, red, concurrencia, MSI, CLI, web) | Guía de código | 1.5 |
| 8 | Diagrama de estados MSI (M/S/I, transiciones, invariantes) | E1/E3 | 1 |
| 9 | Concurrencia señales/sockets (`SIGSEGV`, `sigaction`, `fault_resolver`, hilo de red, locks/semáforos) | E1/E3 | 1.5 |
| 10 | Interbloqueos (grafo de espera, timeouts, orden de locks, reintentos, pruebas) | E1/E3 + fix A1.2 | 1.5 |
| 11 | Estructuras y archivos (tablas, directorio, configuración, logs, tamaños) | E1/guía | 1 |
| 12 | API implementado (tipos, funciones, parámetros, entradas, salidas, errores) | E1/guía | 1 |
| 13 | Protocolo (cabecera, mensajes, payloads, flujos, errores, huella) | E1/guía + A2 | 1 |
| 19 | Conclusiones (resultados, limitaciones, lecciones) | E3 + nuevas pruebas | 1 |

**Criterio de aceptación:** cada sección explica el sistema real, no el diseño ideal; tablas y diagramas referenciados; sin datos inventados; sin IPs reales.

---

### A4. Integración, PDF público y cierre del repo — 4 h

- [ ] Ensamblar `Entrega_Final/04_Entrega_Final.md` desde `secciones/` en el orden oficial 1–21.
- [ ] Revisar numeración, índice, figuras, tablas, referencias y que no haya secciones vacías.
- [ ] Generar la **versión pública PDF** sin carné, profesor, sede ni institución (mismo pipeline que E1/E3).
- [ ] Verificar con `grep` que no queden:
  - IPs reales
  - correos
  - carnés
  - nombres de profesores
  - datos de la institución en la versión pública
- [ ] Actualizar `README.md` final:
  - estado del proyecto
  - 2/3/4 PCs
  - `chat`
  - comandos principales
  - árbol del repositorio
- [ ] Tag `v1.0-final` y release notes.
- [ ] `git log --author` para confirmar commits de ambos.

---

### A5. Demo y defensa técnica — 4 h

- **Tu narración:** arquitectura, MSI, red/protocolo, interbloqueos, huella de configuración.
- **Preparar 20 preguntas** con respuestas cortas:
  - ¿Por qué MSI?
  - ¿Por qué `SIGSEGV`?
  - ¿Cómo se evita el deadlock?
  - ¿Qué pasa si cambia la configuración?
  - ¿Por qué no hay hot reload?
  - ¿Cómo se elige el home?
  - ¿Qué pasa si cae el home?
  - ¿Cómo se mide la latencia?
- **Entrenar a Daniel** en tus temas y aprender los suyos.
- **Grabación de respaldo:** una corrida completa en 2 PCs y otra en 4 loopback, por si falla la red.

---

## 5. Parte B — Daniel

### B0. Prerrequisito: sincronizar el repo — 0.5 h

El commit `914ad66` ya no existe en remoto (se reescribió el historial). Antes de empezar:

```bash
git fetch origin
git reset --hard origin/main
git switch -c daniel/final
git config user.name "Monterrosa"
git config user.email "djmonte2012@gmail.com"
```

Si tiene trabajo sin commitear, que lo guarde en otra rama antes del `reset`.

---

### B1. App `chat`: texto libre entre nodos — 8 h

**Objetivo:** que cada estudiante escriba un mensaje en su terminal y les llegue a los demás, sin cadenas predeterminadas. Esto responde directamente a lo que vieron en las demos de los compañeros.

#### B1.1 Diseño — 1 h

- **No usar barreras por turnos** (rígido y poco natural).
- **Diseño recomendado:** un arreglo DSM de `nodes * page_size`; **una página por nodo** (evita falsos compartidos).
- Cada página tiene un encabezado:

```c
typedef struct chat_slot {
    uint32_t magic;
    uint32_t seq;        /* se actualiza al final, con release */
    uint16_t from;
    uint16_t len;
    uint32_t checksum;   /* FNV-1a del texto */
    uint32_t flags;
    char     text[page_size - 24];
} chat_slot_t;
```

- **Escritor:** copia el texto, calcula checksum, publica `seq+1` con `memory_order_release`.
- **Lector:** lee `seq` (acquire), copia encabezado+texto, vuelve a leer `seq`; si cambió, reintenta (seqlock). Verifica checksum. Si `seq > last_seen[from]`, imprime `[nodo X] texto`.
- **Entrada:** `poll(STDIN_FILENO)` cada 200 ms; `fgets`; límite 512 bytes; UTF-8 se trata como bytes; rechazar caracteres de control.
- **Comandos:** `/quit` (sale del nodo local), `/nick NOMBRE`, `/help`.
- **Opcionales para pruebas:** `--duration S` y `--text "mensaje"` para automatizar.

#### B1.2 Cabecera y API — 1 h

- `include/dsm_apps.h`: `DSM_APP_CHAT = 7` y `int dsm_app_chat_run(...)`.
- `dsm_app_opts_t`: agregar `const char *nick; const char *text; uint32_t duration;` (o reutilizar `file` para el texto inicial).

#### B1.3 Implementación — 4 h

- `src/app_chat.c`.
- `dsm_malloc(nodes * page_size)` + barrera inicial.
- Cada nodo escribe **solo su página**; todos leen todas.
- Imprimir remotos; el propio mensaje se hace eco local.
- No usar `dsm_barrier` en el bucle; solo al inicio y al final.
- Manejo de errores: `dsm_malloc` nulo, nodo único, timeout de barrera.

#### B1.4 Integración — 1 h

- `src/apps.c`: `parse`, `name`, `run`.
- `src/node_main.c`: `--app chat`, `--nick`, `--duration`; ayuda.
- `dsm.sh`: opción 7 `chat`.
- `Makefile`, `build.sh`, `do_build` de `dsm.sh`: agregar `src/app_chat.c`.

#### B1.5 Pruebas — 1 h

- 2 PCs: A escribe “hola desde A”, B lo ve; B responde; ambos se ven.
- 3 y 4 PCs: todos ven a todos; mensajes simultáneos; 100 mensajes; líneas de 512 B; acentos/UTF-8.
- No duplicados, no pérdidas, no lecturas a medias.
- `/quit` cierra solo el nodo local; los demás siguen.
- Evidencia: transcripción de la sesión y logs.

**Criterio de aceptación:** en la demo, el profesor puede dictar una frase y verla aparecer en las otras 3 PCs.

---

### B2. Correcciones de apps y `dsm.sh` multi-nodo — 7 h

#### B2.1 `\n` literales — 0.5 h

Corregir `\\n` → `\n` en:

- `src/app_bench.c:86`
- `src/app_counter.c:107`
- `src/app_file.c:79` y `:93`
- `src/app_matrices.c:67` y `:89`

#### B2.2 `counter` con una página por nodo — 1 h

- `src/app_counter.c`: reservar `nodes * page_size`; ranura `rank * (page_size / 8) + t`; vecino igual.
- Así cada nodo escribe su propia página y no compite por la misma página de 4 KB.
- **Probar:** `100×2` → 400; `2000×4` → 16000 en 3 corridas.

#### B2.3 `file` con datos reales — 1 h

- `src/app_file.c`: aceptar `--file RUTA`; error claro si no existe; imprimir bytes leídos y checksum.
- Crear `Entrega_Final/datos/` con un `.txt` real de ~1 MB (puede generarse desde la documentación) y probar también un archivo propio de cada estudiante.
- Guardar `sha256sum` del archivo fuente como referencia externa; el checksum interno verifica coherencia, no integridad del archivo.

#### B2.4 `dsm.sh` con topología 2/3/4 y roles A–D — 4 h

Hoy el menú acepta C/D pero:

- `do_ips()` solo pide A y B.
- `peer_ip()` solo devuelve A/B.
- `do_start()` y `do_app()` siempre usan `nodes_2.conf`.
- `do_stop()` solo verifica el puerto base.

Cambios:

- [ ] Estado nuevo: `TOPOLOGY` (`2|3|4`) y `IP_A..IP_D`.
- [ ] Opción 1: preguntar topología, luego las IPs de A..D; validar que no se repitan.
- [ ] Generar `config/nodes_2.conf`, `nodes_3.conf`, `nodes_4.conf` (y `nodes_1.conf` para baseline) desde una función `write_nodes_file`.
- [ ] Actualizar `seed_host = IP_A` en `node1..3.conf`.
- [ ] `cfg_for_role()` y `port_for_role()` ya existen; completar `peer_ip()` para C/D.
- [ ] `do_start()` y `do_app()` usan `NODES_FILE=config/nodes_${TOPOLOGY}.conf`.
- [ ] `do_stop()` verifica los **4 puertos** del rol.
- [ ] `do_diag()` prueba todos los peers, no solo uno.
- [ ] `do_status()` mantiene `dir` contra `IP_A`.
- [ ] Menú: mostrar topología; opción “Validar configuración” que ejecute `./dsmctl --config ... config` y muestre la huella.
- [ ] `do_app()`: agregar `chat`; en `file`, preguntar ruta; en `counter`, mantener defaults; en `matrices`, tamaño.
- [ ] `do_build()` y `build.sh`: agregar `app_chat.c`.

**Criterio de aceptación:** en una PC se elige “4”, se cargan 4 IPs, y los 4 nodos arrancan con la misma tabla; `dsmctl nodes` muestra 4.

#### B2.5 `Makefile` / `build.sh` — 0.5 h

- Agregar `app_chat.c` a `NODE_SRC` y al `gcc` de `build.sh`.
- Probar `make clean && make`.

---

### B3. Pruebas 3–4 PCs, evidencia y métricas — 9 h

#### B3.0 Logística — 1 h

- [ ] Reservar 3 PCs primero y 4 después (o confirmar con Sebastián).
- [ ] Verificar puertos `27300–27315` libres, firewall abierto, misma red.
- [ ] Clonar/actualizar el repo en cada PC; `make`.
- [ ] Configurar IPs y topología desde `dsm.sh`; misma tabla en todas.
- [ ] A primero; B/C/D dentro de los 2 minutos.

#### B3.1 Regresión en 2 PCs — 2 h

- Ejecutar las 7 apps: `demo`, `file`, `counter`, `bench`, `evict`, `matrices`, `chat`.
- 3 corridas de `counter 2000×4` y `evict 64/32`.
- Guardar logs y capturas.

#### B3.2 3 PCs — 2.5 h

- `dsmctl nodes` con 3 nodos.
- `matrices 64` y `192`: filas repartidas en 3; verificación OK.
- `file` 1 MB: checksum OK.
- `counter 100×2`: suma 600.
- `bench`.
- `chat`: los 3 se ven.
- Capturas de CLI y web.

#### B3.3 4 PCs — 2.5 h

- `dsmctl nodes` con 4 nodos.
- `matrices 192`: 4 bloques; verificación OK.
- `file` 1 MB y un `.txt` real.
- `counter 100×2`: suma 800; `2000×4` si el fix está estable.
- `bench`.
- `chat`: los 4 se ven; mensajes simultáneos.
- `evict 64/32`; `512` solo como estrés opcional.
- Capturas de CLI y web (`/` y `/stats.json`).

#### B3.4 Métricas — 1 h

- Comparativa **2 vs 4 nodos** y **local vs DSM**.
- Latencia (`bench`), throughput (MB/s), speedup de `matrices`, uso de memoria, fallos, invalidaciones, errores/timeouts.
- Tablas y 2–3 gráficos (pueden ser Mermaid o imágenes).

#### B3.5 Evidencia — 1 h

- `Entrega_Final/evidence/` con logs de texto versionados.
- Capturas con nombres `EF-P1..P6` / `EF-3PC-*` / `EF-4PC-*`; las imágenes van ignoradas y se copian al Word/PDF.
- `git log --graph`, `git shortlog -sn`, `git log --author=Monterrosa`.
- Lista de capturas que necesita Sebastián para el PDF.

---

### B4. Informe: §4–§5, §14–§18, §20–§21 + manual — 8 h

| § | Sección | Contenido | Horas |
|---|---|---|---|
| 4 | Introducción | problema, objetivos, alcance | 1 |
| 5 | Marco teórico | DSM, paginación, MSI, señales, sockets, Pthreads/semáforos | 1.5 |
| 14 | Aplicaciones de prueba | matrices, archivo/video, counter, **chat**, datos reales, salidas esperadas | 1.5 |
| 15 | Pruebas de uso y ejecución | escenarios, resultados 2/3/4 PCs, métricas, problemas | 1.5 |
| 16 | Rendimiento | latencia, throughput, speedup, memoria; 2 vs 4 | 1 |
| 17 | Extras | decisión, qué se implementó, nivel de tolerancia, overhead | 1 |
| 18 | Manual de usuario | requisitos, compilación, configuración, arranque, CLI, web, troubleshooting | 1 |
| 20 | Referencias | libros, POSIX, repositorio | 0.5 |
| 21 | Anexos | logs, capturas, tablas, gráficos, evidencia Git | 1 |

**Manual final:** actualizar `Entregable_3/04_Manual_Usuario.md` o crear `Entrega_Final/05_Manual_Usuario.md`; agregar chat, 3–4 PCs, cambio de config y web.

---

### B5. Demo operativa y defensa — 4 h

- **Tu narración:** apps (`chat`, `matrices`, `file`, `counter`), pruebas, métricas, manual, operación 3–4 PCs, extras.
- **Runbook:** orden de arranque, tiempos, qué se muestra en cada minuto, qué hacer si falla un nodo.
- **Dos ensayos completos** con Sebastián.
- **20 preguntas** de tus temas + aprender los de Sebastián.
- **Respaldo:** video de la corrida y logs listos.

---

## 6. Trabajo compartido

| Actividad | Horas c/u | Cuándo |
|---|---|---|
| Sincronización diaria 15 min | 1.5 | 21–26 nov |
| Revisión cruzada de secciones | 2 | 24–26 nov |
| Ensayos de demo | 2.5 | 26 y 27 nov |
| Integración final y correcciones | 2 | 26 nov |

**Entrenamiento cruzado obligatorio:** cada uno debe poder explicar la parte del otro. La nota es individual y el profesor puede preguntar cualquier módulo.

---

## 7. Calendario 21–27 de noviembre

| Fecha | Sebastián (A) | Daniel (B) | Gate |
|---|---|---|---|
| **Vie 20** | Confirmar 4 PCs, crear ramas/secciones, decidir extras | Reset del repo, rama `daniel/final`, diseño de chat | Alcance congelado |
| **Sáb 21** | A1.1 `counter`, A1.2 `local_drop` | B1.1 diseño + B1.2 cabecera; B2.1 `\n` | — |
| **Dom 22** | A1.3 `evict`, A2 huella | B1.3 `app_chat.c`; B2.2 counter layout | — |
| **Lun 23** | A2 pruebas, inicio A3 §6–§8 | B1.4 integración, B1.5 pruebas 2 PCs, B2.4 `dsm.sh` | **G1:** `make` limpio + chat 2 nodos + topología 4 local |
| **Mar 24** | A3 §9–§13 | **Lab 3 PCs** (B3.2) + B3.1 regresión | **G2:** 3 PCs con todas las apps |
| **Mié 25** | A3 §19 + A4 inicio | **Lab 4 PCs** (B3.3) + B3.4 métricas | **G3:** 4 PCs con matrices/file/chat/counter; decidir Extra 1 |
| **Jue 26** | A4 ensamblado, PDF, README, tag | B4 secciones + manual + B3.5 evidencia | **G4:** informe completo; **código congelado 18:00** |
| **Vie 27** | Ensayo 8:00, demo, defensa | Ensayo 8:00, demo, defensa | **Demo** |

### 7.1 Descripción de los gates

- **G1 (lunes 23):** `make` limpio, chat funcionando en 2 nodos, `dsm.sh` con topología 4 en loopback.
- **G2 (martes 24):** 3 PCs corriendo todas las apps; sin bloqueos críticos.
- **G3 (miércoles 25):** 4 PCs corriendo `matrices`, `file`, `chat`, `counter`; métricas capturadas. **Aquí se decide si se intenta el Extra 1.**
- **G4 (jueves 26):** informe completo, manual, PDF, evidencia; código congelado a las 18:00.
- **Demo (viernes 27):** ensayo 8:00, demo en el horario asignado, respaldo listo.

---

## 8. Balance de horas

| Área | Sebastián | Daniel |
|---|---|---|
| Núcleo/coherencia | 9 | — |
| Apps/chat | — | 8 |
| `dsm.sh`/config multi-nodo | 5 | 7 |
| Informe técnico | 10 | 8 |
| Manual | — | 1 |
| Pruebas/evidencia/métricas | — | 9 |
| Integración/PDF/repo | 4 | 2 |
| Demo/defensa | 4 | 4 |
| Coordinación | 2 | 0.5 |
| **Subtotal** | **34** | **39.5** |
| Compartido | 6 | 6 |
| **Total** | **40** | **45.5** |

Daniel queda con ~5 h más. Para igualar, Sebastián puede tomar §16 (Rendimiento) o §21 (Anexos), o Daniel entrega B3.5 más liviano. **Se ajusta en la sincronización del sábado.**

**Con extras (solo si G3 pasa):**

| Extra | Dueño | Horas | Total |
|---|---|---|---|
| Extra 1 — ejecución distribuida | Daniel | +6 | ~51.5 h |
| Extra 2 — tolerancia (solo diseño) | Sebastián | +2 | ~42 h |

---

## 9. Extras

> Los extras no son obligatorios, pero cada uno vale hasta 10 %. La regla del equipo es: **primero la base estable; extras solo con G3 aprobado**. Si un extra compromete la demo, se documenta como trabajo futuro y se desactiva.

### 9.1 Extra 1 — Ejecución distribuida de procesos (10 %)

**Qué pide:** que desde un nodo se coordine la ejecución de procesos paralelos en otros nodos, usando la memoria distribuida en todo el sistema, pero procesando en múltiples nodos separados de forma local y paralela.

**Decisión propuesta:** intentarlo con una versión de bajo riesgo, **time-boxed a 6 h de Daniel**, solo si G3 pasa.

**Dueño:** Daniel. **Apoyo/revisión:** Sebastián (1 h).

#### Opción A (preferida): coordinador por SSH

- Script `Entrega_Final/cluster_run.sh` o nueva opción en `dsm.sh`.
- Desde A:
  - lanza `dsm_node --app ...` en B/C/D por SSH;
  - usa `nohup` y logs por nodo;
  - espera códigos de salida;
  - recolecta y muestra resultados.
- Requiere SSH sin contraseña o clave configurada en el laboratorio.
- **Ventaja:** demuestra coordinación real de procesos en nodos separados.
- **Riesgo:** SSH bloqueado o sin claves → plan B.

#### Opción B (plan B): app `task` con reparto estático

- `src/app_task.c`: el nodo 0 reparte tareas por rango (`rank`), todos calculan, escriben resultados en DSM, barrera y el nodo 0 verifica.
- No hay planificador dinámico, pero sí coordinación y reparto.
- **Ventaja:** no depende de SSH.
- **Riesgo:** se parece a `matrices`; hay que documentar la diferencia (tareas, balance, recolección).

#### Métricas del Extra 1

- Speedup vs 1 nodo.
- Balance de carga (tareas/filas por nodo).
- Overhead de coordinación (tiempo total menos tiempo de cómputo).
- Throughput.
- Escalabilidad 2 vs 4 nodos.

#### Criterio de aceptación

- 4 nodos ejecutan la misma tarea coordinada.
- Todos reportan resultados.
- El nodo 0 verifica y muestra `OK`.
- Speedup > 1 y overhead documentado.
- Sin regresiones en la base.

#### Si no se puede

- Documentar en §17: diseño, intento, motivo por el que no se completó, trabajo futuro.
- No inventar resultados.

---

### 9.2 Extra 2 — Tolerancia a fallos y redundancia (10 %)

**Qué pide:** considerar la seguridad del sistema ante fallas o caída de nodos o de la conexión de red, con cierto nivel de tolerancia para mantener la integridad y no perder información ante la ausencia esporádica de un nodo.

**Decisión propuesta:** **solo diseño documentado**, sin implementación, para no comprometer la estabilidad de la base.

**Dueño:** Sebastián. **Horas:** 2 h.

#### Contenido mínimo en §17

- Réplica de páginas `M` en uno o más nodos.
- `replication_factor` configurable (ya existe en `dsm_config_t`).
- Mensajes `REPLICA_WRITE` / `REPLICA_ACK`.
- Detección de fallo por heartbeat.
- Promoción de réplica a owner.
- Recuperación de páginas.
- Checkpoints periódicos.
- Log de transiciones.

#### Nivel de tolerancia (diseño)

| `replication_factor` | Nodos que se pueden perder | Condición |
|---|---|---|
| 0 | 0 | Estado actual |
| 1 | 1 | Réplica de cada página `M` en otro nodo |
| 2 | 2 | Dos réplicas; mayor overhead |

> Aclaración: si cae el **home**, la tolerancia sigue limitada salvo que se replique también el directorio. Documentarlo.

#### Overhead (estimado, no medido)

- Espacio: `replication_factor * tamaño de datos`.
- Red: escrituras de réplica y ACKs.
- CPU: coordinación.
- Latencia: esperar ACK de réplica.
- Comparar contra la línea base sin réplica.

#### Criterio de aceptación

- §17 explica el diseño, el nivel de tolerancia y el overhead.
- Dice explícitamente **“no implementado”** y por qué.
- En la demo, ambos pueden explicar la decisión.

---

### 9.3 Regla de corte de extras

| Gate | Decisión |
|---|---|
| G3 aprobado y base estable | Intentar Extra 1 (6 h Daniel) + Extra 2 diseño (2 h Sebastián) |
| G3 aprobado pero poco tiempo | Solo Extra 2 diseño; Extra 1 como trabajo futuro |
| G3 no aprobado | Ningún extra; toda la energía a la base y la demo |

---

## 10. Los tres puntos clave de la demo

### 10.1 Chat con texto libre (no cadenas fijas)

**Problema actual:** `demo_page.c` escribe `"hola desde el nodo 0"` y `"hola desde el nodo 1"`. No permite que el usuario escriba.

**Solución:** `chat` (B1).

Demuestra:

- Texto arbitrario escrito por cada estudiante.
- Una página DSM por nodo → coherencia MSI real (lecturas remotas, invalidaciones).
- 2, 3 y 4 PCs.
- Sin cambiar el protocolo ni el núcleo: usa `dsm_malloc` + memoria compartida + seqlock.

**Guion de demo:**

1. En A: `./dsm.sh` → opción 5 → `chat` → `--nick A`.
2. En B/C/D: lo mismo con sus nombres.
3. El profesor dicta una frase; A la escribe; B/C/D la ven en menos de 1 s.
4. B responde; todos la ven.
5. Mostrar `dsmctl stats` con lecturas remotas.

**Riesgo:** lecturas a medias → seqlock + checksum + reintento; probar 100 mensajes.

---

### 10.2 Pruebas con 3 y 4 computadoras

**Problema actual:** `dsm.sh` solo pide A/B y siempre usa `nodes_2.conf`; los roles C/D están a medias.

**Solución:** B2.4 + B3.

- Topología 2/3/4 en el menú.
- IPs A–D y tablas `nodes_2/3/4.conf`.
- Rol A–D por PC; puertos `27300 + 4*id`.
- `seed_host` de B/C/D = A.
- `dsmctl nodes` debe mostrar 3 o 4.
- Evidencia por PC: logs, capturas, `dsmctl`, web.

**Guion de demo:**

- Mostrar 2 PCs primero (respaldo).
- Luego 4 PCs:
  - `dsmctl nodes`
  - `matrices 192` repartido
  - `chat` con los 4
  - `file` con checksum
- Si solo hay 3, mostrar 3 y explicar que el diseño escala a 4/16.

**Plan B:** si el laboratorio solo presta 2 PCs, correr 4 nodos en loopback en una PC y documentar la limitación; no inventar una prueba de 4 PCs.

---

### 10.3 Cambio de configuración en medio del proceso

**Respuesta correcta para la demo (hoy):**

> “La configuración se lee **una sola vez al arrancar**. No hay recarga en caliente: si editás el archivo mientras el nodo corre, el proceso no cambia. Si reiniciás un nodo con otra IP o puerto, los peers siguen con la tabla vieja; la conexión TCP se corta y el sistema falla rápido (`fail-stop`), no reconecta a la dirección nueva. Para cambiar la configuración hay que detener todos los nodos, actualizarla en todas las PCs, verificar que la tabla sea idéntica y arrancar A primero y luego B/C/D.”

**Por qué es una decisión de diseño:** cambiar membresía, IPs o puertos en caliente exige épocas/quórum y puede romper la coherencia MSI. No es un bug; es una limitación deliberada y documentada.

**Mejora para la final (A2):**

- Huella de configuración en cada mensaje (`reserved2`).
- Si dos nodos tienen tablas/parámetros distintos → error explícito `DSM_ERR_CONFIG_MISMATCH`, no timeout.
- `dsmctl config` muestra la huella; en la demo se compara en las 4 PCs.
- Manual: sección “Reconfigurar el clúster” con el procedimiento exacto.

**Guion de demo:**

1. Mostrar `dsmctl config` en A y B: misma huella.
2. Cambiar `page_size` solo en B (en vivo, sin reiniciar A).
3. Arrancar B: error claro de configuración incompatible.
4. Restaurar y reiniciar ordenadamente.
5. Cerrar con: “los puertos se eligen automáticamente por rol; la edición manual queda para casos avanzados”.

---

## 11. Riesgos y plan B

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Solo hay 2 PCs | Alto | 2 PCs + 4 loopback; documentar; no inventar 4 PCs |
| Red/firewall del laboratorio | Alto | Probar puertos el 24; llevar config de respaldo; hablar con el encargado |
| `counter` no queda 100 % | Medio | App con una página por nodo; si falla, usar `100×2` en demo y documentar |
| `evict` 4n/512p inestable | Medio | Default `64/32`; 512 solo estrés opcional |
| Chat con lecturas a medias | Medio | Seqlock + checksum + reintento; probar 100 mensajes |
| Web no responde | Bajo | `curl` en Linux Mint; si falla, documentar y no bloquear |
| Daniel sin commits suficientes | Alto | Commits diarios, ramas/PRs, revisión de `git shortlog` el jueves |
| Conflictos de Markdown | Medio | Un archivo por sección en `Entrega_Final/secciones/`; ensamblado final |
| Tiempo insuficiente | Alto | Base primero; extras solo con G3; congelar el jueves 18:00 |
| Datos sensibles en el repo | Alto | IPs de ejemplo; `git grep` antes de push; PDF público sanitizado |
| Un integrante no puede explicar | Alto | Entrenamiento cruzado y ensayos |
| Git no muestra autoría | Medio | Verificar `git shortlog -sn` antes de la demo |

---

## 12. Definición de terminado y checklist de cierre

### 12.1 Código

- [ ] `make clean && make` sin errores ni warnings.
- [ ] `./dsm.sh` funciona en 2, 3 y 4 PCs.
- [ ] Las 7 apps corren: `demo`, `file`, `counter`, `bench`, `evict`, `matrices`, `chat`.
- [ ] `counter 2000×4` = 16000 (o limitación documentada).
- [ ] `evict 64/32` estable; `512` documentado.
- [ ] `chat` entrega texto libre en 2/3/4 nodos.
- [ ] `dsmctl nodes/stats/pages/dir/config` funcionan.
- [ ] Web responde `/` y `/stats.json` (o limitación documentada).
- [ ] Sin secretos, IPs reales ni datos personales en el código.

### 12.2 Documentación

- [ ] `Entrega_Final/04_Entrega_Final.md` con las 21 secciones.
- [ ] Portada, resumen ejecutivo e índice.
- [ ] Diagramas de arquitectura, despliegue y estados MSI.
- [ ] API y protocolo completos.
- [ ] Aplicaciones y pruebas con resultados reales.
- [ ] Manual de usuario final.
- [ ] Conclusiones y referencias.
- [ ] Anexos con logs, tablas, gráficos y evidencia Git.
- [ ] PDF público sin datos sensibles.

### 12.3 Demo

- [ ] 4 PCs confirmadas (o plan B documentado).
- [ ] Puertos y firewall probados.
- [ ] Configuración de respaldo.
- [ ] Datos de prueba listos.
- [ ] Guion de 110 minutos ensayado dos veces.
- [ ] Grabación de respaldo.
- [ ] Ambos integrantes pueden explicar todas las partes.
- [ ] Preguntas frecuentes preparadas.

### 12.4 Git

- [ ] Commits de Sebastián y Daniel en varias fechas.
- [ ] `git shortlog -sn` muestra a ambos.
- [ ] `README` actualizado.
- [ ] Tag `v1.0-final`.
- [ ] Repositorio público con el PDF final.

---

## 13. Próximos pasos

### Hoy (viernes 20)

1. **Sebastián:**
   - confirmar 4 PCs y la cita del 27;
   - crear `Entrega_Final/secciones/`;
   - decidir extras;
   - enviar a Daniel las instrucciones de reset del repo.
2. **Daniel:**
   - hacer `git fetch origin && git reset --hard origin/main`;
   - crear la rama `daniel/final`;
   - leer este plan;
   - empezar el diseño de `chat` (B1.1).
3. **Ambos:**
   - sincronización de 15 min;
   - confirmar la división o intercambiar un bloque si hace falta.

### Sábado 21

- Sebastián: A1.1 `counter` y A1.2 `local_drop`.
- Daniel: B1.1 diseño + B1.2 cabecera + B2.1 `\n`.

### Domingo 22

- Sebastián: A1.3 `evict` + A2 huella.
- Daniel: B1.3 `app_chat.c` + B2.2 counter layout.

---

## 14. Anexos: convenciones y comandos

### 14.1 Convención de commits

| Tipo | Ejemplo |
|---|---|
| Feature | `feat(chat): agrega app de mensajes con texto libre` |
| Fix | `fix(counter): corrige la cesion de propiedad del home` |
| Refactor | `refactor(dsm.sh): soporta topologia de 3 y 4 nodos` |
| Tests | `test(lab): agrega corrida de 4 PCs y metricas` |
| Docs | `docs(final): agrega seccion de rendimiento` |
| Build | `build: agrega app_chat al Makefile` |

**Reglas:**

- Mensajes en español, imperativo, ≤ 72 caracteres en la primera línea.
- Un commit por cambio lógico.
- Daniel y Sebastián commitean con su propia identidad.
- No usar `--author` para simular autoría.

### 14.2 Convención de evidencia

```text
Entrega_Final/evidence/
├── EF-2PC-matrices-A.log
├── EF-3PC-chat-B.log
├── EF-4PC-matrices-192-A.log
├── EF-4PC-file-A.log
├── EF-4PC-counter-A.log
├── EF-4PC-dsmctl-nodes-A.txt
├── EF-4PC-web-stats-A.json
└── EF-git-log.txt
```

- Logs de texto: versionados.
- Capturas `.png/.jpg`: ignoradas; se copian al Word/PDF.
- Nombres sin IPs reales, sin carnés, sin nombres de profesores.

### 14.3 Comandos útiles

```bash
# Compilar
cd Entregable_3/prototipo2
make clean && make

# Menú principal
./dsm.sh

# Loopback (desarrollo)
./lab.sh demo
./lab.sh matrices --size 64
./lab.sh file
./lab.sh counter 100 2
./lab.sh bench
./lab.sh evict 64 32

# Consultas
./dsmctl --config config/node0.conf nodes
./dsmctl --config config/node0.conf stats
./dsmctl --config config/node0.conf pages
./dsmctl --config config/node0.conf dir
./dsmctl --config config/node0.conf config

# Web
curl -s http://IP:27303/ | head
curl -s http://IP:27303/stats.json

# Git
git log --oneline --graph --all
git shortlog -sn
git log --author="Monterrosa" --oneline
git log --author="Sebastian" --oneline
```

### 14.4 Puertos por rol

| Rol | Nodo | Puertos |
|---|---|---|
| A | 0 (home) | 27300–27303 |
| B | 1 | 27304–27307 |
| C | 2 | 27308–27311 |
| D | 3 | 27312–27315 |

Fórmula: `base = 27300 + 4 * node_id`.

### 14.5 Enlaces

- Repositorio: <https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida>
- Documento maestro: `00_Proyecto_DSM_General.md`
- Entregable 3: `Entregable_3/03_Entregable_3_Prototipo_2.md`
- Manual de usuario: `Entregable_3/04_Manual_Usuario.md`
- Guía de código: `Entregable_3/03_Entregable_3_Guia_Codigo.md`
- Instrucciones de la Entrega Final: `Entrega_Final/INSTRUCCIONES.md`

---

*Fin del plan. Cualquier cambio de alcance, dueño o fecha se registra en la tabla de control de versiones antes de ejecutarse.*
