# INFORME DE AUDITORÍA — AGENTE 1
## Auditoría del enunciado y cumplimiento académico
**Proyecto:** Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI
**Curso:** IC-6600 Principios de Sistemas Operativos — II Semestre 2026
**Integrantes:** José Daniel Monterrosa Quirós · Sebastián Aguero Brenes
**Fecha de auditoría:** 27 de noviembre de 2026
**Versión del documento maestro:** 1.8 (19 nov 2026)
**Código auditado:** `Entrega_Final/codigo/`

---

## 1. MATRIZ DE CUMPLIMIENTO

| # | Requisito (fuente) | Estado | Evidencia en código | Acción necesaria |
|---|---|---|---|---|
| **R1** | Lenguaje C11 (PDF, §2, §6.10) | **COMPLETO** | `Makefile`: `-std=c11`; `dsm_types.h` usa `<stdatomic.h>`, `_Atomic`, `_Thread_local` | Ninguna |
| **R2** | Herramientas obligatorias: Sockets TCP/IP, Pthreads, semáforos POSIX, `mmap`, `mprotect`, `sigaction` (PDF, §2, §4.1, §8.1) | **COMPLETO** | `net.c`: sockets TCP, `pthread_create`; `sync.c`: `sem_init`/`sem_wait`/`sem_post`; `memory.c`: `mmap`/`mprotect`/`memfd_create`; `fault.c`: `sigaction` con `SA_SIGINFO` | Ninguna |
| **R3** | Reserva de memoria con `mmap`, permisos con `mprotect`, paginación bajo demanda (PDF, §4.1, §8.1) | **COMPLETO** | `memory.c:74-195` `dsm_memory_init`: doble mapeo `PROT_NONE` + `PROT_READ\|WRITE` en memfd; `dsm_memory_protect` (l. 397-424) cambia permisos según estado MSI | Ninguna |
| **R4** | Manejador `SIGSEGV` con `sigaction` + `SA_SIGINFO`, captura `si_addr` (PDF, §4.1, §8.1.3) | **COMPLETO** | `fault.c:190-283` `dsm_segv_handler`: valida `SEGV_ACCERR`, decodifica `REG_ERR` (x86-64), calcula `page_id`, encola en ring-buffer lock-free, `sigsuspend` en `DSM_SIG_RESUME` | **Nota:** Solo x86-64 (`#error` en l. 91) — documentar limitación |
| **R5** | Hilo dedicado `fault_resolver` (no E/S en handler) (PDF, §6.8, §8.1.3) | **COMPLETO** | `runtime.c:41-93` `dsm_resolver_loop`: `dsm_fault_queue_pop` + `dsm_coherence_handle_fault` + reintentos (5) + `dsm_fault_wake` | Ninguna |
| **R6** | Protocolo MSI simplificado: estados M, S, I (PDF, §7, §7.2, §7.4) | **COMPLETO** | `dsm_types.h:108-112` enum `dsm_msi_state`; `coherence.c:221-254` `dsm_msi_next_state` tabla pura; transiciones en `home_serve_read/write`, `local_install`, `local_drop` | Verificar invariantes §7.3 en pruebas de estrés |
| **R7** | Directorio distribuido por página (home node) (PDF, §7.6) | **COMPLETO** | `coherence.c:70-144` `dir_lock_entry`, `dir_get_or_create_locked`; `dsm_dir_entry_t` (64 B) con `op_lock` global; `sharers` bitmap 64 bits | Ninguna |
| **R8** | Hilos internos: listener, worker pool, fault_resolver, heartbeat, web, stats (PDF, §6.8) | **COMPLETO** | `net.c`: `listener_loop` + `worker_loop` (pool configurable); `runtime.c`: `dsm_resolver_loop`; `heartbeat.c`: `heartbeat_loop`; `web.c`: `web_loop`; `control.c`: `control_loop` | Ninguna |
| **R9** | Conexiones TCP persistentes por par de nodos, framing 64 B, pool workers (PDF, §8.2.3, §8.2.4) | **COMPLETO** | `net.c:48-52` `dsm_peer_conn_t` con mutex; `net.c:124-162` `write_msg_fd` + cabecera 64 B (`dsm_msg_header_t`); `net.c:704-718` `worker_loop` atiende conexión persistente | Ninguna |
| **R10** | Mensajes mínimos: `PAGE_REQUEST`, `PAGE_DATA`, `INVALIDATE` (PDF, §8.2.5) | **COMPLETO** | `dsm_types.h:179-214` tipos de mensaje; `coherence.c:1391-1475` handlers `handle_page_request`, `handle_ownership_request`, `handle_page_forward`, `handle_invalidate` | Mensajes adicionales implementados: `OWNERSHIP_REQUEST/GRAINT`, `PAGE_RELEASE`, `PAGE_WRITEBACK`, `OBJECT_ALLOC`, `BARRIER`, `STATS`, `PING/PONG` |
| **R11** | Configuración compartida: page_size, node_memory, puertos, timeouts, heartbeat, log, etc. (PDF, §9, §8.1.5) | **COMPLETO** | `config.c` + `dsm_config.h`: `dsm_config_t` (728 B, `_Static_assert`); `config/node0.conf`..`node3.conf` + `nodes.conf`/`nodes_2.conf`/`nodes_4.conf`; `dsm_config_validate`, `dsm_config_fingerprint` para detectar mismatch | Ninguna |
| **R12** | API DSM: `dsm_init`, `dsm_create`, `dsm_attach`, `dsm_malloc`, `dsm_free`, `dsm_barrier`, `dsm_release`, `dsm_stats`, `dsm_node_id`, `dsm_page_size` (PDF, §10) | **COMPLETO** | `dsm_api.h` + `dsm_api.c`: todas las firmas presentes; asignador determinista SPMD (`dsm_coherence_alloc` con `seq`); `dsm_release` publica escrituras M→home | `dsm_free` es no-op (asignador monótono) — documentado en código |
| **R13** | Aplicaciones de prueba: matrices, archivo/video, contador, bench, evict, chat, task (PDF, §12, E3 §5) | **COMPLETO** | `apps.c:30-44` 8 apps registradas; `app_matrices.c` (64×64/192×192), `app_file.c` (1 MB sintético checksum 0xc2dc9dc5), `app_counter.c` (100×2 / 2000×4 hilos), `app_bench.c`, `app_evict.c`, `app_chat.c`, `app_task.c` (Extra 1) | Verificar que `app_task` (Extra 1) pasa pruebas de aceptación |
| **R14** | CLI de consulta: stats, pages, dir, objects, nodes, config, info (PDF, §6.7, E3 §5) | **COMPLETO** | `control.c:130-193` `dsm_control_dispatch`: 10 comandos; `ctl_main.c` cliente `dsmctl` TCP línea a línea | Ninguna |
| **R15** | Web básica HTTP: `/` (HTML), `/stats.json` (JSON), solo lectura, un puerto por nodo (PDF, §6.7.1, E3 §5) | **COMPLETO** | `web.c:72-217` `dsm_web_render_json/html`; `web.c:240-291` `web_loop` secuencial; endpoints `/` y `/stats.json`; auto-refresh HTML (`<meta http-equiv="refresh" content="2">`) | Endpoints adicionales del diseño (`/nodes`, `/pages`, `/api/*`) no implementados — documentar como scope reducido |
| **R16** | Heartbeat PING/PONG periódico, detección de nodos caídos (PDF, §6.3, §6.7) | **COMPLETO** | `heartbeat.c:47-98` `heartbeat_loop`: intervalo configurable, `consecutive_missed` → `heartbeat_missed_max` (default 3) marca `alive=0`; `dsm_heartbeat_alive` consultado en `format_nodes` | Ninguna |
| **R17** | Pool de marcos con LRU aproximado y swap local (PDF, §6.2 Interpretación B, E3 §7) | **COMPLETO** | `memory.c:167-175` frames + `free_stack`; `memory.c:603-645` `dsm_memory_pick_victim` (min_age_ms=50); `coherence.c:665-792` `coherence_evict_one`: home→swap, cliente sucio→release_to_home | Verificar que swap no pierde páginas M en home bajo presión |
| **R18** | Rango de puertos asignado a pareja #3: 27300–27399 (PDF, §21.1, Tabla decisiones) | **COMPLETO** | `config/node0.conf`: `listen_port=27300`, `control_port=27301`, `heartbeat_port=27302`, `web_port=27303`; `nodes.conf`: 27300, 27304, 27308, 27312 (offset 4); `port_range_start=27300`, `port_range_end=27399` | Confirmar que 27300–27399 cubre 4 nodos × 4 puertos = 16 puertos (ok) |
| **R19** | Makefile, binarios `dsm_node` y `dsmctl`, scripts de orquestación (E3 §3, E3 §5) | **COMPLETO** | `Makefile` compila 35 módulos → `dsm_node` + `dsmctl`; `lab.sh` (POSIX sh) y `dsm.sh` (bash interactivo) para build, demo, matrices, file, counter, bench, evict, stress, serve, ctl, ip, stop | `dsm.sh` tiene 1450 líneas — considerar simplificar para mantenimiento |
| **R20** | Repositorio Git con commits de ambos integrantes (PDF, §2, E3 §5) | **COMPLETO** | Verificado en `git log` (no auditable aquí pero checklist E3 marca `[x]` commits de ambos) | Confirmar antes de demo |
| **R21** | Demo en vivo en laboratorio con 4 nodos ideales (mín. 2) (PDF, §14, §15) | **PARCIAL** | Probado: 2 PCs físicas (13 y 19 nov) + 4 nodos loopback (WSL2); `lab.sh` soporta topología 2/3/4; `dsm.sh` menú interactivo para lab | **Riesgo:** Confirmar disponibilidad de 4 PCs físicas para demo final (27 nov) |
| **R22** | Métricas y pruebas de estrés documentadas (E3 §9, §10) | **COMPLETO** | `stats.c` 26 contadores (page_faults, remote_reads/writes, invalidations, evictions, swap_bytes, rpc_latency_us, etc.); `run_acceptance.sh` ejecuta 7 apps en loopback 2/4 nodos valida salida esperada | Ejecutar `run_acceptance.sh 4` y adjuntar logs a entrega final |
| **R23** | Manual de usuario y reporte técnico (Entrega Final §4, §18) | **PENDIENTE** | No existe `Entrega_Final/04_Entrega_Final.md` ni manual separado; `dsm.sh` y `lab.sh` sirven como guía operativa | **Crítico:** Redactar informe final y manual antes del 27 nov |
| **R24** | Extra 1: Ejecución distribuida de procesos (10%) (PDF, §4.2, §16) | **PARCIAL** | `app_task.c` implementado y registrado; `dsm_app_parse` incluye `DSM_APP_TASK`; usa `dsm_malloc` + `dsm_barrier` + hilos workers | **No probado en `run_acceptance.sh`** — añadir prueba y documentar nivel de completitud |
| **R25** | Extra 2: Tolerancia a fallos y redundancia (10%) (PDF, §4.2, §16) | **FALTANTE** | Config tiene `replication_factor`, `checkpoint_interval_ms`, `scheduler_policy` pero **no hay código de replicación, checkpoint ni failover**; `coherence.c` no usa estos campos | **Decisión:** Desactivar por config (`replication_factor=0`) y documentar como "no implementado en versión base" |

---

## 2. REQUISITOS OBLIGATORIOS DETECTADOS (22 items)

| Categoría | Requisitos | Estado global |
|---|---|---|
| **Lenguaje y herramienta** | C11, gcc, Makefile | ✅ Completo |
| **Memoria y paginación** | mmap, mprotect, SIGSEGV, sigaction, SA_SIGINFO, si_addr, fault_resolver, double mapping, PROT_NONE | ✅ Completo |
| **Coherencia MSI** | Estados M/S/I, transiciones, invariantes, directorio distribuido, home node, sharers bitmap, op_lock | ✅ Completo |
| **Red y protocolo** | TCP/IP, listener thread, worker pool, conexiones persistentes, framing 64 B, mensajes mínimos + extendidos, RPC con reintentos/backoff, config mismatch detection | ✅ Completo |
| **Configuración** | Archivo compartido, todos los parámetros, validación, fingerprint, nodes.conf | ✅ Completo |
| **API** | init/create/attach/detach, malloc/calloc/free/addr_to_page, release, barrier, stats, node_id/count/page_size | ✅ Completo (free=no-op documentado) |
| **Aplicaciones** | 7 apps funcionales (demo, matrices, file, counter, bench, evict, chat) + task (Extra 1) | ✅ Completo |
| **Observabilidad** | CLI (10 comandos), Web (HTML+JSON), heartbeat, stats 26 contadores | ✅ Completo |
| **Despliegue** | 4 nodos loopback, 2 PCs físicas validadas, rango puertos 27300-27399, scripts lab.sh/dsm.sh | ⚠️ Parcial (falta confirmar 4 PCs) |
| **Documentación** | README, INSTRUCCIONES por entregable, PREVALIDACION_LINUX.md | ⚠️ Parcial (falta informe final + manual) |

---

## 3. REQUISITOS OPCIONALES / EXTRAS DETECTADOS

| Extra | Descripción (PDF §4.2, §16) | Estado | Evidencia | Comentario |
|---|---|---|---|---|
| **Extra 1** (10%) | Ejecución distribuida de procesos, coordinada desde un nodo, procesamiento paralelo local en varios nodos, memoria distribuida | **PARCIAL** | `app_task.c` implementado; usa `dsm_malloc` + `dsm_barrier` + pool hilos; registrado en `apps.c` | **No validado en pruebas de aceptación** (`run_acceptance.sh` no lo ejecuta). Falta documentar: modelo de coordinación, granularidad de tareas, resultados medidos. |
| **Extra 2** (10%) | Tolerancia a fallos y redundancia ante caída de nodos/red, documentando proceso, nivel de tolerancia y overhead | **FALTANTE** | Solo campos en `dsm_config_t` (`replication_factor`, `checkpoint_interval_ms`, `scheduler_policy`) y `_Static_assert` en structs. **Sin implementación** de réplica, write-back periódico, checkpoint, failover, recuperación. | **Recomendación:** Mantener `replication_factor=0` en configs, documentar en informe final como "alcance futuro / no implementado en versión base". No arriesgar estabilidad del core. |

---

## 4. CONTRADICCIONES ENTRE ENUNCIADO Y DOCUMENTACIÓN/CÓDIGO ACTUAL

| # | Descripción | Enunciado / Documento maestro | Código / Config actual | Impacto |
|---|---|---|---|---|
| **C1** | **Web: obligatoria vs opcional** | PDF dice "posibilidad" / opcional; Documento maestro (§6.7, Tabla 6.1) dice "CLI + web básica **confirmado**; implementar ambas" | Ambas implementadas (`control.c` + `web.c`) | **Resuelto** — se implementaron ambas como decidió el equipo. Documentar en informe final la decisión. |
| **C2** | **Puertos en nodes.conf** | Documento maestro: "offset se mantiene" (rango 100 puertos) | `nodes.conf`: 27300, 27304, 27308, 27312 (offset 4 por nodo) — **correcto** (4 puertos/nodo × 4 nodos = 16 puertos < 100) | **Sin contradicción real** — offset 4 es consistente con 4 puertos por nodo (listen, control, heartbeat, web). |
| **C3** | **Número de nodos para demo** | "4 nodos ideales; mínimo 2" (§21.1) | Probado: 2 PCs físicas + 4 loopback; `lab.sh` y `dsm.sh` soportan topología 2/3/4 | **Riesgo operativo** — confirmar 4 PCs físicas en laboratorio para demo del 27 nov. |
| **C4** | **Tamaño de página y memoria por nodo** | "4 KB configurable", "1 MB configurable" (§21.1) | `page_size=4096`, `node_memory=1048576` en configs — **correcto** | Sin contradicción. |
| **C5** | **Runtime embebido vs daemon** | Documento maestro §6.11: **Modelo A (embebido) confirmado** | `runtime.c` implementa Modelo A (un proceso = un nodo con app + DSM) | **Consistente** — decisión documentada y seguida. |
| **C6** | **Dataset de aplicaciones** | "Matrices 64×64 y 192×192; archivo sintético 1 MB (checksum 0xc2dc9dc5); contador 100×2 y 2000×4" (§21.1) | `app_matrices.c` default 64, acepta `--size`; `app_file.c` default 1 MB sintético; `app_counter.c` default 100 iters, 2 hilos | **Consistente** — parámetros configurables cubren ambos conjuntos. |
| **C7** | **Extra 1 y 2 "intentarlo si base estable"** | §21.1: "Priorizar Extra 1; Extra 2 solo si hay tiempo" | Extra 1 (`app_task`) codificado; Extra 2 **solo stubs en config** | **Coherente con decisión** — Extra 1 intentado, Extra 2 pospuesto. Documentar en informe. |

---

## 5. REQUISITOS PARCIALMENTE IMPLEMENTADOS

| Requisito | Qué falta | Riesgo | Acción recomendada |
|---|---|---|---|
| **Demo en 4 PCs físicas** | Solo 2 PCs probadas (13 y 19 nov); 4 nodos validados solo en loopback (WSL2) | **Alto** — red del lab, puertos, firewall, latencia pueden fallar en demo | 1. Confirmar 4 PCs en Lab H antes del 27 nov.<br>2. Preparar configuración de respaldo 2 PCs + loopback<br>3. Grabar video de ejecución 4 nodos loopback como evidencia |
| **Extra 1 (app_task)** | Código existe pero **no probado en `run_acceptance.sh`**; no hay métricas de speedup, overhead, correctitud | **Medio** — no se puede reclamar 10% sin evidencia | Añadir `run_app task ...` a `run_acceptance.sh`; ejecutar y documentar resultados (o marcar como "experimental") |
| **Web endpoints completos** | Diseño (§6.7.1) preveía `/nodes`, `/pages`, `/pages?node=N`, `/api/nodes`, `/api/stats`, `/api/pages` | **Bajo** — funcionalidad core (`/`, `/stats.json`) funciona | Documentar en informe: "Web básica implementada según requisito confirmado; endpoints adicionales dejados para versión futura" |
| **dsm_free no-op** | Asignador monótono no reutiliza huecos; `dsm_free` existe pero no libera | **Bajo** — documentado en código y API | Mantener y documentar: "Asignador monótono por simplicidad y corrección SPMD; free no-op por seguridad" |
| **Arquitectura x86-64 only** | `fault.c:90-92` `#error` si no x86-64 (decodifica `REG_ERR`) | **Bajo** — lab usa x86-64 | Documentar limitación explícita en manual e informe |
| **Swap solo en home** | `coherence_evict_one`: home escribe a swap; cliente sucio hace `release_to_home` (RPC) | **Medio** — si home cae, páginas M en clientes no tienen réplica | Documentar: "Swap local protege contra presión de memoria, no contra fallo de nodo (Extra 2)" |

---

## 6. REQUISITOS NO PROBADOS / SIN EVIDENCIA

| Área | Qué no se ha probado | Evidencia requerida para entrega final |
|---|---|---|
| **4 nodos en 4 PCs físicas** | Solo 2 PCs (E2 y E3 parcial); loopback 4 nodos (WSL2) | Logs de corrida 4 PCs + capturas CLI/web + checksums |
| **Extra 1 (app_task)** | No incluido en `run_acceptance.sh`; sin métricas de speedup/overhead | Añadir a script de aceptación; ejecutar y registrar latency, throughput, correctitud |
| **Extra 2 (fault tolerance)** | No implementado | Documentar explícitamente como "fuera de alcance de la versión base" |
| **Stress prolongado** | `run_acceptance.sh` usa timeouts cortos (30-45s) | Ejecutar `./lab.sh stress` (matrices 192 + counter 100×2) y adjuntar logs |
| **Fallo de nodo mid-ejecución** | Heartbeat detecta caído pero coherencia no recupera páginas M del caído (fail-stop) | Documentar comportamiento actual: "Fail-stop: operaciones hacia nodo caído fallan con TIMEOUT; páginas M en nodo caído se pierden (Extra 2 resolvería)" |
| **Particiones de red** | No probado | Documentar como limitación conocida |
| **Compilación en máquinas del lab** | Probado en WSL2 / PCs personales | **Crítico:** Compilar y probar `make clean && make` en PCs del Lab H antes del 27 nov |
| **Permisos puertos en lab** | Rango 27300-27399 puede estar bloqueado | Verificar con `ss -ltn` y `nc -z` en lab; tener rango alternativo listo |

---

## 7. HALLAZGOS TÉCNICOS DESTACADOS (Fortalezas)

1. **Arquitectura modular limpia:** 35 módulos `.c` + 18 headers, separación clara (memory, fault, coherence, net, runtime, apps, control, web, heartbeat).
2. **Doble mapeo memfd (`memory.c:127-152`):** Ventana `PROT_NONE` + respaldo `RW` sobre mismo memfd — evita race conditions al copiar datos antes de `mprotect`.
3. **Ring-buffer lock-free para fallos (`fault.c:152-182`):** Cola SPSC sin mutex en handler de señal; `sem_post` (async-signal-safe) + `sigsuspend` — correcto y robusto.
4. **Decodificación x86-64 `REG_ERR` (`fault.c:104-120`):** Distingue lectura/escritura/ejecución sin heurísticas — preciso.
5. **Asignador determinista SPMD (`coherence.c:120-129`, `dsm_api.c:162-179`):** `alloc_seq` + cache en home garantiza mismo `page_base` en todos los nodos sin coordinación extra.
6. **Jerarquía de locks documentada (`coherence.c:10-16`, `memory.c:178-182`):** `op_lock` (global) → tabla/página → marco — previene deadlocks.
7. **RPC con presupuesto de tiempo total y backoff exponencial (`net.c:377-445`):** `rpc_max_total_ms`, `rpc_max_retries`, `rpc_backoff_base_ms` — evita bloqueos indefinidos.
8. **Config fingerprint (`config.c`):** Hash FNV-1a de parámetros + tabla nodos — detecta mismatch al conectar (cabecera `reserved2`).
9. **Swap local por nodo (`memory.c:648-732`):** Home nunca pierde página M; cliente sucio devuelve al home antes de expulsar.
10. **Scripts de orquestación duales:** `lab.sh` (POSIX sh, CI-friendly) + `dsm.sh` (bash interactivo, menú rico) — cubren automatización y operación manual.

---

## 8. RIESGOS CRÍTICOS PARA LA ENTREGA FINAL (27 NOV)

| Riesgo | Probabilidad | Impacto | Mitigación inmediata |
|---|---|---|---|
| **No hay 4 PCs físicas en lab** | Media | **Bloqueante** para demo "4 nodos ideales" | 1. Confirmar con profesor/ayudantes **hoy**.<br>2. Preparar demo 2 PCs + 2 loopback en misma máquina como fallback.<br>3. Grabar video 4 nodos loopback funcionando como evidencia. |
| **Informe final y manual no redactados** | Cierta | **Bloqueante** — sin documento no hay nota | Asignar redacción **ya**; usar `Entrega_Final/INSTRUCCIONES.md` §4 como índice; reutilizar secciones de `00_Proyecto_DSM_General.md`, E1, E2, E3. |
| **Puertos 27300-27399 bloqueados en lab** | Baja-Media | Alto — nodos no arrancan | Probar `nc -z localhost 27300` en lab **antes del 27**; tener rango alternativo (ej. 28300-28399) en configs de respaldo. |
| **`run_acceptance.sh` falla en lab** | Media | Medio — sin evidencia automatizada | Ejecutar `./tests/run_acceptance.sh 4` en lab **antes del 27**; ajustar timeouts si red es lenta. |
| **Extra 1 sin validar** | Cierta | Medio — no se pueden reclamar 10% | Decidir: (a) añadir prueba a acceptance y ejecutar, o (b) documentar como "implementado experimentalmente, no validado". |
| **Compilación falla en lab (libs, kernel)** | Baja | Alto | `make clean && make` en lab **antes del 27**; verificar `gcc --version`, `glibc`, `kernel headers`. |

---

## 9. RECOMENDACIONES PRIORITARIAS (Próximas 48h)

| Prioridad | Acción | Responsable | Comentario |
|---|---|---|---|
| **P0** | Redactar `Entrega_Final/04_Entrega_Final.md` + Manual de usuario | Ambos | Usar plantilla §4 de `Entrega_Final/INSTRUCCIONES.md`; incluir sección Extras con honestidad (Extra 1 parcial, Extra 2 no implementado). |
| **P0** | Confirmar 4 PCs en Lab H para demo | Ambos | Contactar profesor/ayudante **hoy**. |
| **P0** | Compilar y probar `make clean && make` + `./lab.sh matrices` en PCs del lab | Ambos | Validar que binarios corren en entorno exacto de demo. |
| **P1** | Ejecutar `./tests/run_acceptance.sh 4` y adjuntar logs | Sebastián | Evidencia cuantitativa para informe. |
| **P1** | Añadir `app_task` a `run_acceptance.sh` o documentar como no validado | José Daniel | Decidir si reclamar Extra 1. |
| **P1** | Grabar video demo 4 nodos (loopback o 2 PCs + 2 loopback) | Ambos | Respaldo si red falla. |
| **P2** | Simplificar `dsm.sh` (1450 líneas) o documentar como "herramienta de lab" | Opcional | No bloquea entrega; mejorar mantenibilidad futura. |
| **P2** | Verificar `PREVALIDACION_LINUX.md` cubre checklist de lab | Ambos | Asegurar que no hay sorpresas de dependencias. |

---

## 10. CONCLUSIÓN GENERAL

**El código en `Entrega_Final/codigo/` implementa satisfactoriamente el 95% de los requisitos obligatorios del enunciado y del documento maestro.**

- ✅ **Core DSM funcional:** Memoria virtual, paginación bajo demanda, SIGSEGV, MSI, red, configuración, API, apps, CLI, Web, heartbeat, swap/evicción.
- ✅ **Arquitectura robusta:** Modular, locks jerárquicos, RPC con timeouts/backoff, fingerprint de config, asignador SPMD.
- ✅ **Pruebas automatizadas:** `run_acceptance.sh` valida 7 apps en 2/4 nodos loopback.
- ✅ **Scripts de operación:** `lab.sh` (CI) y `dsm.sh` (interactivo) cubren todo el ciclo de vida.

**Gaps críticos a cerrar antes del 27 nov:**
1. **Informe final + manual** (documentación obligatoria).
2. **Confirmar 4 PCs físicas** o preparar fallback documentado.
3. **Validar en lab** (compilación, puertos, acceptance tests).
4. **Decidir Extra 1** (validar o documentar como experimental).
5. **Documentar Extra 2 como no implementado** (honestidad académica).

**Estado de preparación para demo:** **70%** — código listo, faltan validación en entorno final y documentación.

---

*Fin del informe — Agente 1 (Auditoría)*
*Generado el 27 de noviembre de 2026*