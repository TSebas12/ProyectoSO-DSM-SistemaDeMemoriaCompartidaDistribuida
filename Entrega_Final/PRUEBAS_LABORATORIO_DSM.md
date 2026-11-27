# Guía de Pruebas en Laboratorio — DSM (Sistema de Memoria Compartida Distribuida)

> **Documento autónomo y reproducible** — Cualquier integrante del equipo puede ejecutar todas las pruebas sin explicaciones verbales adicionales.
>
> **Versión:** 1.0 | **Proyecto:** IC-6600 Principios de Sistemas Operativos — Entregable Final
>
> **Binarios:** `dsm_node` (nodo DSM), `dsmctl` (CLI de diagnóstico)
>
> **Directorio de trabajo:** `Entrega_Final/codigo/`

---

## 1. Matriz de Topologías de Prueba

**Distinción clave:** *PCs físicas* vs *procesos/nodos DSM*. Una PC puede ejecutar varios nodos DSM (loopback).

| Topología | PCs Físicas | Nodos DSM | Descripción | Archivo `nodes_*.conf` |
|---|---|---|---|---|
| **T1** | 1 | 1 | Baseline secuencial (referencia) | `nodes_1.conf` |
| **T2** | 1 | 2 | Dos procesos en una PC (loopback) | `nodes_2.conf` |
| **T3** | 1 | 3 | Tres procesos en una PC | `nodes_3.conf` (crear) |
| **T4** | 1 | 4 | Cuatro procesos en una PC | `nodes_4.conf` |
| **T5** | 2 | 2 | Dos PCs físicas (1 nodo c/u) | `nodes_2.conf` (IPs reales) |
| **T6** | 2 | 4 | Dos PCs con 2 nodos c/u | `nodes_4.conf` (IPs reales) |
| **T7** | 3 | 3 | Tres PCs físicas | `nodes_3.conf` (IPs reales) |
| **T8** | 4 | 4 | Cuatro PCs físicas | `nodes_4.conf` (IPs reales) |

> **Notación en comandos:**
> - **PC-A, PC-B, PC-C, PC-D** = computadoras físicas (roles A=home/nodo 0, B=nodo 1, C=nodo 2, D=nodo 3)
> - **NODE-0, NODE-1, NODE-2, NODE-3** = procesos DSM
> - **IPs de ejemplo (RFC 5737):** `192.0.2.10` (PC-A), `192.0.2.11` (PC-B), `192.0.2.12` (PC-C), `192.0.2.13` (PC-D)
> - **NO usar IPs reales del laboratorio** en esta guía; sustituir al ejecutar.

---

## 2. Estructura Estándar de Cada Prueba

Todas las pruebas siguen esta plantilla de **13 elementos**:

| # | Elemento | Descripción |
|---|---|---|
| 1 | **ID y Nombre** | Identificador único (PR-XX) y nombre descriptivo |
| 2 | **Objetivo** | Qué se verifica |
| 3 | **Tipo** | Positiva / Negativa / Estrés / Integración / Rendimiento |
| 4 | **Topología** | T1–T8 (ver matriz) |
| 5 | **Requisitos Previos** | Qué debe estar listo antes de iniciar |
| 6 | **Configuración Necesaria** | Archivos `.conf`, IPs, puertos, variables |
| 7 | **Comandos EXACTOS** | Separados por PC/nodo, listos para copiar-pegar |
| 8 | **Pasos Numerados** | Secuencia de ejecución |
| 9 | **Resultado Esperado** | Salida en pantalla, códigos de salida, métricas |
| 10 | **Evidencia a Guardar** | Capturas, logs, JSON, screenshots |
| 11 | **Criterios de Aprobación** | Condiciones objetivas PASS/FAIL |
| 12 | **Troubleshooting** | Qué hacer si falla (causas probables y soluciones) |
| 13 | **Limpieza Posterior** | Comandos para dejar el entorno limpio |

---

## 3. Catálogo de Pruebas (40+)

### 3.1 Arranque y Conectividad (PR-01 a PR-10)

---

#### **PR-01: Iniciar 1 nodo (baseline secuencial)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-01 |
| **Nombre** | Baseline 1 nodo / 1 PC |
| **Objetivo** | Verificar arranque correcto del nodo home sin peers; sirve de referencia temporal y de memoria para todas las demás pruebas. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • Binarios compilados (`dsm_node`, `dsmctl`)<br>• Directorio `logs/` creado<br>• Puerto 27300–27303 libres |
| **Configuración** | `config/node0.conf` + `config/nodes_1.conf` (solo `0 127.0.0.1 27300`) |
| **Comandos EXACTOS** | ```bash<br># PC-A (única PC, NODE-0 = home)<br>PC-A$ cd Entrega_Final/codigo<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_1.conf --app demo --serve<br>``` |
| **Pasos** | 1. Ejecutar comando en PC-A<br>2. Verificar mensaje `nodo 0: listo. control=27301 web=27303 (activa)`<br>3. Esperar 5 s<br>4. Enviar `Ctrl+C` para detener |
| **Resultado Esperado** | • Arranque sin errores<br>• `control` y `web` puertos activos<br>• Exit code 0 al detener con Ctrl+C |
| **Evidencia** | Captura de terminal con arranque y `listo`; `logs/dsm-node0.log` |
| **Criterios Aprobación** | PASS si: arranca, imprime `listo`, no hay `FALLO`/`ERROR`/`SIGSEGV`, exit 0 |
| **Troubleshooting** | • **Puerto ocupado:** `ss -ltnp | grep 27300` → matar proceso previo (`pkill -f dsm_node`)<br>• **Config inválida:** ejecutar `./dsm_node --config config/node0.conf --nodes config/nodes_1.conf --check-config`<br>• **Falta `logs/`:** `mkdir -p logs` |
| **Limpieza** | `pkill -f dsm_node`; verificar `pgrep -f dsm_node \|\| echo "OK"` |

---

#### **PR-02: Iniciar 2 nodos (1 PC, loopback)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-02 |
| **Nombre** | 2 nodos en 1 PC (loopback) |
| **Objetivo** | Verificar comunicación RPC entre dos procesos DSM en la misma máquina vía loopback; validar handshake, heartbeat y directorio. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-01 aprobado<br>• Puertos 27300–27307 libres |
| **Configuración** | `config/node0.conf` (home), `config/node1.conf` (cliente), `config/nodes_2.conf` (`0 127.0.0.1 27300` / `1 127.0.0.1 27304`) |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — PC-A (NODE-0 = home)<br>PC-A$ cd Entrega_Final/codigo<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --serve<br><br># Terminal 2 — PC-A (NODE-1)<br>PC-A$ cd Entrega_Final/codigo<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf<br>``` |
| **Pasos** | 1. Abrir **dos terminales** en la misma PC<br>2. En Terminal 1: arrancar NODE-0 con `--serve` (queda escuchando)<br>3. Esperar `nodo 0: listo`<br>4. En Terminal 2: arrancar NODE-1 (sin `--serve`)<br>5. Verificar que NODE-1 conecta, imprime `listo` y termina<br>6. En Terminal 1: `Ctrl+C` |
| **Resultado Esperado** | • NODE-0: `nodo 0: listo...`<br>• NODE-1: `nodo 1: listo...` y exit 0<br>• Logs muestran heartbeat mutuo |
| **Evidencia** | Captura ambas terminales; `logs/dsm-node0.log`, `logs/dsm-node1.log` |
| **Criterios Aprobación** | PASS si: ambos arrancan, NODE-1 termina con exit 0, NODE-0 sigue vivo |
| **Troubleshooting** | • **NODE-1 no conecta:** verificar `seed_host = 127.0.0.1` y `seed_port = 27300` en `node1.conf`<br>• **Config mismatch:** `--check-config` en ambos; huella debe coincidir<br>• **Firewall local:** `sudo ufw allow 27300:27307/tcp` (si aplica) |
| **Limpieza** | `pkill -f dsm_node` en ambas terminales; `ss -ltn | grep 2730` debe estar vacío |

---

#### **PR-03: Iniciar 3 nodos (1 PC, loopback)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-03 |
| **Nombre** | 3 nodos en 1 PC |
| **Objetivo** | Verificar escalabilidad de directorio y coherencia con 3 nodos en loopback. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T3** (1 PC / 3 nodos) |
| **Requisitos Previos** | • PR-02 aprobado<br>• Crear `config/nodes_3.conf`:<br>  `0 127.0.0.1 27300`<br>  `1 127.0.0.1 27304`<br>  `2 127.0.0.1 27308`<br>• Puertos 27300–27311 libres |
| **Configuración** | `node0.conf`, `node1.conf`, `node2.conf` + `nodes_3.conf` |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — NODE-0 (home)<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_3.conf --serve<br><br># Terminal 2 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_3.conf<br><br># Terminal 3 — NODE-2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_3.conf<br>``` |
| **Pasos** | 1. Abrir 3 terminales<br>2. Arrancar NODE-0 con `--serve`, esperar `listo`<br>3. Arrancar NODE-1, esperar `listo` + exit 0<br>4. Arrancar NODE-2, esperar `listo` + exit 0<br>5. `Ctrl+C` en NODE-0 |
| **Resultado Esperado** | Tres nodos arrancan; NODE-1 y NODE-2 terminan OK; NODE-0 sigue vivo |
| **Evidencia** | 3 capturas terminal; 3 logs |
| **Criterios Aprobación** | PASS: 3/3 nodos `listo`, 2/2 clientes exit 0, sin errores |
| **Troubleshooting** | • **Puerto 27308/27309/27310/27311 ocupado:** liberar con `fuser -k`<br>• **Timeout conexión:** aumentar `connect_timeout_ms` en configs si la máquina es lenta |
| **Limpieza** | `pkill -f dsm_node`; verificar puertos 27300–27311 libres |

---

#### **PR-04: Iniciar 4 nodos (1 PC, loopback)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-04 |
| **Nombre** | 4 nodos en 1 PC (máximo local) |
| **Objetivo** | Verificar clúster completo de 4 nodos en una sola máquina; prueba de estrés de puertos y hilos. |
| **Tipo** | Positiva / Integración / Estrés |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-03 aprobado<br>• `config/nodes_4.conf` ya existe<br>• Puertos 27300–27315 libres |
| **Configuración** | `node0.conf`..`node3.conf` + `nodes_4.conf` |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — NODE-0 (home)<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --serve<br><br># Terminal 2 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf<br><br># Terminal 3 — NODE-2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf<br><br># Terminal 4 — NODE-3<br>PC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf<br>``` |
| **Pasos** | 1. 4 terminales<br>2. NODE-0 con `--serve` → `listo`<br>3. NODE-1 → `listo` + exit 0<br>4. NODE-2 → `listo` + exit 0<br>5. NODE-3 → `listo` + exit 0<br>6. `Ctrl+C` en NODE-0 |
| **Resultado Esperado** | 4 nodos `listo`; 3 clientes exit 0; home sigue vivo |
| **Evidencia** | 4 capturas; 4 logs |
| **Criterios Aprobación** | PASS: 4/4 `listo`, 3/3 exit 0, sin `ERROR`/`FALLO` |
| **Troubleshooting** | • **Demasiados archivos abiertos:** `ulimit -n 4096`<br>• **RAM insuficiente:** cada nodo reserva 1 MB (`node_memory`); 4 MB totales + overhead |
| **Limpieza** | `pkill -f dsm_node`; `ss -ltn | grep 273` vacío |

---

#### **PR-05: Arrancar nodos en distinto orden**

| Elemento | Detalle |
|---|---|
| **ID** | PR-05 |
| **Nombre** | Arranque desordenado (race de arranque) |
| **Objetivo** | Verificar que el sistema tolera que los nodos no arranquen simultáneamente ni en orden 0→1→2→3. |
| **Tipo** | Positiva / Negativa (caso borde) |
| **Topología** | **T4** (1 PC / 4 nodos) o **T8** (4 PCs) |
| **Requisitos Previos** | • PR-04 aprobado |
| **Configuración** | Misma que PR-04 |
| **Comandos EXACTOS** | ```bash<br># Orden: 2 → 0 → 3 → 1<br># Terminal 1 — NODE-2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf<br><br># Terminal 2 — NODE-0 (home) — CON --serve<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --serve<br><br># Terminal 3 — NODE-3<br>PC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf<br><br># Terminal 4 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf<br>``` |
| **Pasos** | 1. Arrancar NODE-2 primero (fallará reintentando conexión a home)<br>2. A los 3 s, arrancar NODE-0 (home) con `--serve`<br>3. NODE-2 debe conectar y terminar OK<br>4. Arrancar NODE-3 y NODE-1 en cualquier orden<br>5. Todos deben terminar OK<br>6. `Ctrl+C` en NODE-0 |
| **Resultado Esperado** | • NODE-2 muestra reintentos (`retry_count=5`, `retry_backoff_ms=500`) y luego conecta<br>• Todos terminan exit 0<br>• Home sigue vivo |
| **Evidencia** | Capturas mostrando reintentos y posterior conexión exitosa |
| **Criterios Aprobación** | PASS: todos conectan finalmente, exit 0, sin timeout fatal |
| **Troubleshooting** | • **NODE-2 agota reintentos:** aumentar `retry_count` o `retry_backoff_ms` en config<br>• **Home tarda en estar listo:** NODE-2 debe reintentar hasta que home acepte conexiones |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-06: Iniciar nodo con demora (2 min)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-06 |
| **Nombre** | Nodo tardío (2 minutos) |
| **Objetivo** | Verificar que un nodo puede unirse al clúster después de 2 minutos de estar los demás activos (heartbeat y directorio estables). |
| **Tipo** | Positiva / Integración |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-04 aprobado |
| **Configuración** | Misma que PR-04; `heartbeat_interval_ms=1000`, `heartbeat_timeout_ms=3000` |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — NODE-0 (home) con --serve<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --serve<br><br># Terminal 2 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf<br><br># Terminal 3 — NODE-2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf<br><br># ESPERAR 2 MINUTOS (120 s)<br><br># Terminal 4 — NODE-3 (tardío)<br>PC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf<br>``` |
| **Pasos** | 1. Arrancar NODE-0, NODE-1, NODE-2 normalmente<br>2. Verificar que los 3 están `listo` y NODE-1/2 terminaron<br>3. Esperar **exactamente 2 minutos** (cronómetro)<br>4. Arrancar NODE-3<br>5. Verificar conexión exitosa |
| **Resultado Esperado** | NODE-3 conecta sin problemas; directorio ya tiene 3 nodos vivos; NODE-3 se registra y termina OK |
| **Evidencia** | Captura NODE-3 conectando; log NODE-0 mostrando registro tardío |
| **Criterios Aprobación** | PASS: NODE-3 exit 0, NODE-0 log muestra `nodo 3 registrado` |
| **Troubleshooting** | • **NODE-3 marcado como muerto:** home debe tener `heartbeat_timeout_ms` suficiente (3 s por defecto); 2 min >> 3 s, pero home no expulsa por heartbeat si el nodo nunca se registró<br>• **Directorio lleno:** `max_nodes=16` por defecto, 4 nodos OK |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-07: Espera >15 s no provoca cierre prematuro**

| Elemento | Detalle |
|---|---|
| **ID** | PR-07 |
| **Nombre** | Tolerancia a latencia de arranque (>15 s) |
| **Objetivo** | Confirmar que el timeout de conexión (`connect_timeout_ms=5000` + reintentos) permite esperas >15 s sin abortar. |
| **Tipo** | Positiva / Negativa (límite) |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02 aprobado |
| **Configuración** | `node0.conf`, `node1.conf`, `nodes_2.conf`; `connect_timeout_ms=5000`, `retry_count=5`, `retry_backoff_ms=500` → max ~5 s + 5×0.5 s = 7.5 s por intento; pero el cliente reintenta hasta conectar |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — NODE-0 (home) --serve<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --serve<br><br># ESPERAR 20 SEGUNDOS<br><br># Terminal 2 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf<br>``` |
| **Pasos** | 1. Arrancar home<br>2. Esperar **20 segundos** (más que tiempo total de reintentos)<br>3. Arrancar cliente<br>4. Observar: debe conectar tras reintentos |
| **Resultado Esperado** | Cliente reintenta (logs muestran `reintentando...`), home acepta, cliente termina OK |
| **Evidencia** | Log cliente mostrando reintentos y éxito final |
| **Criterios Aprobación** | PASS: cliente exit 0 tras reintentos; no aborta por timeout global |
| **Troubleshooting** | • Si aborta: verificar que `retry_count` y `retry_backoff_ms` permiten cobertura de 20 s<br>• Ajustar: `retry_count=20`, `retry_backoff_ms=1000` para 20 s |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-08: Conectarse a nodo no iniciado**

| Elemento | Detalle |
|---|---|
| **ID** | PR-08 |
| **Nombre** | Conexión a home inexistente (negativa) |
| **Objetivo** | Verificar manejo de error cuando el cliente intenta conectar a un home que no está corriendo. |
| **Tipo** | Negativa |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • Binarios listos<br>• **NO arrancar home** |
| **Configuración** | `node1.conf`, `nodes_2.conf` (home en 127.0.0.1:27300) |
| **Comandos EXACTOS** | ```bash<br># SOLO cliente — home NO corriendo<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf<br>``` |
| **Pasos** | 1. Confirmar que NO hay proceso en puerto 27300 (`ss -ltn | grep 27300` vacío)<br>2. Ejecutar cliente NODE-1<br>3. Observar comportamiento |
| **Resultado Esperado** | • Cliente reintenta `retry_count` veces (5 por defecto)<br>• Tras agotar reintentos: imprime error de conexión y sale con **exit ≠ 0** (código 1 o 3)<br>• No cuelga indefinidamente |
| **Evidencia** | Captura terminal con reintentos y error final |
| **Criterios Aprobación** | PASS: exit ≠ 0 tras reintentos finitos; no hang |
| **Troubleshooting** | • Si cuelga: bug en `net.c` connect loop — reportar<br>• Si exit 0: bug — debería fallar |
| **Limpieza** | Nada (no hay procesos vivos) |

---

#### **PR-09: Puerto ocupado**

| Elemento | Detalle |
|---|---|
| **ID** | PR-09 |
| **Nombre** | Puerto en uso (conflicto de binding) |
| **Objetivo** | Verificar detección temprana y mensaje claro cuando el puerto de escucha ya está ocupado. |
| **Tipo** | Negativa |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • Puerto 27300 ocupado artificialmente: `nc -l 27300 &` |
| **Configuración** | `node0.conf`, `nodes_1.conf` |
| **Comandos EXACTOS** | ```bash<br># Ocupar puerto 27300<br>PC-A$ nc -l 27300 &<br>NC_PID=$!<br>sleep 1<br><br># Intentar arrancar nodo<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_1.conf<br><br># Limpiar<br>PC-A$ kill $NC_PID<br>``` |
| **Pasos** | 1. Ocupar puerto 27300 con `nc -l`<br>2. Intentar arrancar `dsm_node`<br>3. Verificar error de binding<br>4. Liberar puerto y reintentar (debe funcionar) |
| **Resultado Esperado** | • Error inmediato: `bind: Address already in use` o similar<br>• Exit code ≠ 0<br>• Tras liberar puerto, arranque normal |
| **Evidencia** | Captura error binding + arranque exitoso tras liberar |
| **Criterios Aprobación** | PASS: error claro, exit ≠ 0, recuperación tras liberar |
| **Troubleshooting** | • Si no detecta: revisar `SO_REUSEADDR` en `net.c`<br>• Usar `ss -ltnp` para ver qué proceso ocupa el puerto |
| **Limpieza** | `kill $NC_PID`; `pkill -f dsm_node` |

---

#### **PR-10: Detener clúster ordenadamente**

| Elemento | Detalle |
|---|---|
| **ID** | PR-10 |
| **Nombre** | Shutdown grácil (SIGINT/SIGTERM) |
| **Objetivo** | Verificar que `Ctrl+C` (SIGINT) y `kill` (SIGTERM) cierran todos los hilos, liberan puertos y escriben logs finales. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-04 aprobado (clúster 4 nodos corriendo con home en `--serve`) |
| **Configuración** | Clúster activo: NODE-0 (`--serve`), NODE-1,2,3 terminaron |
| **Comandos EXACTOS** | ```bash<br># En terminal de NODE-0 (home)<br>Ctrl+C<br><br># Verificar puertos libres<br>PC-A$ ss -ltn | grep 2730<br>``` |
| **Pasos** | 1. Tener home corriendo con `--serve` (otros nodos ya terminaron)<br>2. Enviar `Ctrl+C` (SIGINT)<br>3. Verificar mensaje `nodo 0: deteniendo...`<br>4. Verificar que puertos 27300–27303 quedan libres<br>5. Verificar `logs/dsm-node0.log` tiene entrada de cierre |
| **Resultado Esperado** | • Mensaje `deteniendo...`<br>• Exit code 0<br>• Puertos libres en < 2 s<br>• Log cerrado correctamente |
| **Evidencia** | Captura `Ctrl+C` → mensaje → prompt; `ss -ltn` vacío en puertos 27300–27303 |
| **Criterios Aprobación** | PASS: SIGINT manejado, puertos libres, exit 0, log completo |
| **Troubleshooting** | • **Puertos quedan ocupados:** hilos no terminaron — revisar `dsm_runtime_destroy()` y join de hilos<br>• **Log truncado:** `setvbuf(stdout, _IOLBF)` en `node_main.c` línea 126 ya lo fuerza |
| **Limpieza** | Ya limpio; verificar `pgrep -f dsm_node \|\| echo "OK"` |

---

### 3.2 Memoria y Capacidad (PR-11 a PR-20)

---

#### **PR-11: Lectura/escritura básica**

| Elemento | Detalle |
|---|---|
| **ID** | PR-11 |
| **Nombre** | R/W básica en página compartida |
| **Objetivo** | Verificar operaciones fundamentales: `dsm_alloc`, `dsm_write`, `dsm_read`, `dsm_free` con coherencia MSI. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02 aprobado<br>• App `demo` o `matrices` funcional |
| **Configuración** | `node0.conf`, `node1.conf`, `nodes_2.conf` |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — NODE-0 (home) --serve<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app demo --serve<br><br># Terminal 2 — NODE-1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app demo<br>``` |
| **Pasos** | 1. Arrancar home con `--app demo --serve` (ejecuta demo y queda escuchando)<br>2. Arrancar cliente con `--app demo`<br>3. Demo escribe/lee página compartida, hace 3 barreras<br>4. Verificar salida: `página compartida OK`, `barreras: 3/3`<br>5. `Ctrl+C` en home |
| **Resultado Esperado** | • Ambos nodos: `fallos de página: 2` (1 por nodo aprox), `reenvios: 1`, `barreras: 3`, `errores: 0`<br>• Exit 0 en ambos |
| **Evidencia** | Capturas ambos nodos; logs con contadores de fallos/reenvios |
| **Criterios Aprobación** | PASS: `errores: 0`, `barreras: 3`, exit 0 ambos |
| **Troubleshooting** | • **Fallos ≠ 2:** revisar `dsm_fault.c` manejo de page fault<br>• **Barrera < 3:** timeout de barrera — aumentar `barrier_timeout_ms` |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-12: Fallos de página y RPC**

| Elemento | Detalle |
|---|---|
| **ID** | PR-12 |
| **Nombre** | Page faults → RPC fetch/invalidate |
| **Objetivo** | Verificar que accesos a páginas no residentes disparan page fault, RPC al owner, transferencia de datos y actualización de estado MSI. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-11 aprobado |
| **Configuración** | Misma; `log_level = 2` (DEBUG) en ambos `.conf` para ver trazas de fault |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — home<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 64 --serve<br><br># Terminal 2 — cliente<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 64<br>``` |
| **Pasos** | 1. Arrancar home con matrices 64 (carga ligera, muchos fallos de página)<br>2. Arrancar cliente<br>3. Observar logs: `PAGE_FAULT`, `RPC_FETCH`, `RPC_INVALIDATE`, estado MSI (S/M/I)<br>4. Verificar `verificacion: OK (0 discrepancias)`<br>5. `Ctrl+C` home |
| **Resultado Esperado** | • Logs muestran secuencia: fault → fetch → data → install → estado S/M<br>• `fallos de página > 0` en ambos<br>• `invalidaciones > 0` (cuando home invalida copias)<br>• Verificación OK |
| **Evidencia** | `logs/dsm-node0.log` y `node1.log` con trazas DEBUG; captura verificación OK |
| **Criterios Aprobación** | PASS: trazas completas fault→fetch→install, verificación OK, exit 0 |
| **Troubleshooting** | • **Sin trazas DEBUG:** `log_level = 2` en config; reiniciar<br>• **Faults = 0:** páginas ya residentes — usar `--size` mayor o `--frames` menor |
| **Limpieza** | `pkill -f dsm_node`; restaurar `log_level = 1` (INFO) |

---

#### **PR-13: Compartición MSI (estado S — Shared)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-13 |
| **Nombre** | Estado Shared (múltiples lectores) |
| **Objetivo** | Verificar que varias lecturas concurrentes dejan la página en estado **S** (Shared) en múltiples nodos sin invalidaciones. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-04 aprobado |
| **Configuración** | `nodes_4.conf`; app `matrices --size 64` (lectura mayoritaria) |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — home<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 64 --serve<br><br># Terminal 2 — nodo 1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 64<br><br># Terminal 3 — nodo 2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 64<br><br># Terminal 4 — nodo 3<br>PC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size 64<br>``` |
| **Pasos** | 1. Arrancar home con `--serve`<br>2. Arrancar 3 clientes simultáneamente<br>3. Matrices 64 hace lecturas mayoritarias → estado S<br>4. Verificar en logs: `estado: S` en múltiples nodos para misma página<br>5. `dsmctl stats` en home durante ejecución (opcional) |
| **Resultado Esperado** | • `dsmctl --config config/node0.conf pages` muestra páginas con `estado=S` y `sharers>1`<br>• `invalidaciones = 0` o muy bajo (solo al inicio)<br>• 4/4 verificaciones OK |
| **Evidencia** | `dsmctl pages` output; logs con estado S |
| **Criterios Aprobación** | PASS: páginas en S con sharers>1, invalidaciones mínimas, 4/4 OK |
| **Troubleshooting** | • **Invalidaciones altas:** bug en coherencia — revisar `coherence.c` transición S→I<br>• **Estado siempre M:** owner no comparte — revisar `dsm_coherence_handle_read()` |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-14: Invalidación (estado I — Invalid)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-14 |
| **Nombre** | Invalidación por escritura (estado I) |
| **Objetivo** | Verificar que una escritura invalida copias remotas (transición S→I) y el escritor pasa a **M** (Modified). |
| **Tipo** | Positiva / Integración |
| **Topología** | **T3** (1 PC / 3 nodos) |
| **Requisitos Previos** | • PR-13 aprobado |
| **Configuración** | `nodes_3.conf`; app `counter` (escrituras concurrentes en misma página) |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — home<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_3.conf --app counter --iters 200 --threads 2 --serve<br><br># Terminal 2 — nodo 1<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_3.conf --app counter --iters 200 --threads 2<br><br># Terminal 3 — nodo 2<br>PC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_3.conf --app counter --iters 200 --threads 2<br>``` |
| **Pasos** | 1. Arrancar home con counter (muchas escrituras en contador compartido)<br>2. Arrancar 2 clientes<br>3. Counter incrementa variable compartida → invalidaciones frecuentes<br>4. Verificar logs: `INVALIDATE` enviado, `estado: I` en receptores<br>5. Resultado final: `suma = 1200, esperado = 1200, OK` (3 nodos × 2 hilos × 200) |
| **Resultado Esperado** | • `invalidaciones > 100` en stats<br>• Páginas pasan S→I→M cíclicamente<br>• Verificación counter OK |
| **Evidencia** | `dsmctl stats` mostrando `invalidaciones`; captura counter OK |
| **Criterios Aprobación** | PASS: invalidaciones > 0, counter verifica OK, exit 0 todos |
| **Troubleshooting** | • **Contador incorrecto:** race condition — revisar atomicidad en `dsm_coherence_handle_write()`<br>• **Página no invalida:** directorio no envía invalidate — revisar `protocol.c` |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-15: Transferencia propiedad (estado M — Modified)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-15 |
| **Nombre** | Ownership transfer (M → M remoto) |
| **Objetivo** | Verificar transferencia de ownership: nodo A tiene página en M, nodo B escribe → A envía datos + ownership, B pasa a M, A pasa a I. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-14 aprobado |
| **Configuración** | `nodes_2.conf`; app `bench` (patrón write-read alternado) |
| **Comandos EXACTOS** | ```bash<br># Terminal 1 — home<br>PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app bench --iters 500 --serve<br><br># Terminal 2 — cliente<br>PC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app bench --iters 500<br>``` |
| **Pasos** | 1. Arrancar home con bench (alterna read/write en páginas)<br>2. Arrancar cliente<br>3. Bench imprime latencias: `local_read`, `remote_read`, `write`\n4. Verificar que `remote_read` > `local_read` (transferencia ownership)\n5. Logs muestran `OWNERSHIP_TRANSFER` o secuencia `INVALIDATE`+`FETCH`+`DATA` |
| **Resultado Esperado** | • Latencia `remote_read` ~2-5× `local_read` (red loopback)\n• Transferencias de ownership contadas en stats\n• Exit 0 ambos |
| **Evidencia** | Output bench con latencias; `dsmctl stats` transferencias |
| **Criterios Aprobación** | PASS: remote_read > local_read, transferencias > 0, exit 0 |
| **Troubleshooting** | • **remote_read ≈ local_read:** página ya local — aumentar working set (`--pages` en bench)\n• **Sin transferencias:** página siempre en home — patrón de acceso no fuerza ownership change |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-16: Capacidad progresiva (25% → 100%)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-16 |
| **Nombre** | Carga progresiva de memoria |
| **Objetivo** | Verificar comportamiento del sistema al llenar la memoria DSM progresivamente: 25%, 50%, 75%, 100% de `node_memory`. |
| **Tipo** | Rendimiento / Estrés |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02 aprobado<br>`node_memory = 1048576` (1 MB = 256 páginas de 4 KB) |
| **Configuración** | `nodes_2.conf`; app `evict` con `--pages` variable, `--frames` fijo (32) |
| **Comandos EXACTOS** | ```bash<br># 25% (64 páginas)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 64 --frames 32 --serve &\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 64 --frames 32\nwait\n\n# 50% (128 páginas)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 128 --frames 32 --serve &\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 128 --frames 32\nwait\n\n# 75% (192 páginas)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 192 --frames 32 --serve &\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 192 --frames 32\nwait\n\n# 100% (256 páginas)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 256 --frames 32 --serve &\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 256 --frames 32\nwait\n``` |
| **Pasos** | 1. Ejecutar 4 series incrementales (esperar cada una con `wait`)<br>2. Anotar: `expulsiones`, `swap_bytes`, `residentes`, `libres`, tiempo<br>3. Verificar `verificacion OK` en cada serie |
| **Resultado Esperado** | • 25%: expulsiones ≈ 0, swap ≈ 0<br>• 50%: expulsiones > 0, swap > 0<br>• 75%: expulsiones ↑, swap ↑<br>• 100%: expulsiones máximas, swap máximo<br>• Todas: `verificacion OK` |
| **Evidencia** | Tabla con métricas por nivel; capturas `verificacion OK` cada nivel |
| **Criterios Aprobación** | PASS: 4/4 niveles `verificacion OK`, métricas coherentes (expulsiones crecen) |
| **Troubleshooting** | • **Falla en 75-100%:** OOM o LRU bug — revisar `memory.c` eviction<br>• **Swap no crece:** `eviction_policy` — 0 = LRU, verificar implementación |
| **Limpieza** | `pkill -f dsm_node` entre series si quedan procesos |

---

#### **PR-17: Capacidad máxima (100%)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-17 |
| **Nombre** | Memoria al 100% (límite teórico) |
| **Objetivo** | Verificar operación sostenida con working set = memoria física asignada (256 páginas, 32 marcos → presión 8:1). |
| **Tipo** | Estrés / Rendimiento |
| **Topología** | **T4** (1 PC / 4 nodos) — máxima presión |
| **Requisitos Previos** | • PR-16 aprobado (100% en 2 nodos OK) |
| **Configuración** | `nodes_4.conf`; `--pages 256 --frames 32` en los 4 nodos |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app evict --pages 256 --frames 32 --serve &\n\n# Terminal 2 — nodo 1\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app evict --pages 256 --frames 32 &\n\n# Terminal 3 — nodo 2\nPC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app evict --pages 256 --frames 32 &\n\n# Terminal 4 — nodo 3\nPC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app evict --pages 256 --frames 32\nwait\n``` |
| **Pasos** | 1. Arrancar home con `--serve`\n2. Arrancar 3 clientes en paralelo (`&` + `wait`)\n3. Monitorear: `expulsiones`, `swap_bytes`, `residentes=32`, `libres=0`\n4. Verificar `verificacion OK` en los 4\n5. `Ctrl+C` home |
| **Resultado Esperado** | • 4/4 `verificacion OK`\n• `expulsiones` ≈ 256-32 = 224 por nodo\n• `swap_bytes` > 0 (home escribe a disco)\n• `residentes = 32`, `libres = 0` estable |
| **Evidencia** | 4 capturas verificación; stats finales home |
| **Criterios Aprobación** | PASS: 4/4 OK, métricas en límites esperados, sin crash |
| **Troubleshooting** | • **Crash en nodo:** OOM — `ulimit -v` o `node_memory` muy alto para 4 procesos\n• **Deadlock:** eviction bloqueada — revisar `fault_queue_size` y hilos |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-18: Superar límites controlado**

| Elemento | Detalle |
|---|---|
| **ID** | PR-18 |
| **Nombre** | Overcommit (páginas > memoria máxima) |
| **Objetivo** | Verificar comportamiento cuando la aplicación solicita más páginas de las que caben en `node_memory` (debe fallar grácilmente en `dsm_alloc`). |
| **Tipo** | Negativa / Límites |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • Binarios listos |
| **Configuración** | `node0.conf` (`node_memory=1048576` = 256 páginas); app `evict --pages 300 --frames 32` |
| **Comandos EXACTOS** | ```bash\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_1.conf --app evict --pages 300 --frames 32\n``` |
| **Pasos** | 1. Ejecutar con `--pages 300` (> 256 max)\n2. Observar error en `dsm_alloc`\n3. Verificar exit ≠ 0 y mensaje claro |
| **Resultado Esperado** | • Error: `dsm_alloc: sin marcos disponibles` o `ENOMEM`\n• Exit code ≠ 0 (1 o 3)\n• No crash/SIGSEGV |
| **Evidencia** | Captura error de asignación |
| **Criterios Aprobación** | PASS: error controlado, exit ≠ 0, sin corrupción |
| **Troubleshooting** | • **SIGSEGV:** bug en `memory.c` bounds check\n• **Exit 0:** no valida límite — revisar `dsm_alloc()` |
| **Limpieza** | Nada (proceso termina solo) |

---

#### **PR-19: Agotamiento marcos (evict bajo presión extrema)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-19 |
| **Nombre** | Frame exhaustion (marcos = 1) |
| **Objetivo** | Forzar evicción continua con solo 1 marco por nodo (thrashing) y verificar que el sistema no deadlockea. |
| **Tipo** | Estrés / Negativa (límite) |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-17 aprobado |
| **Configuración** | `--frames 1 --pages 64` (64 páginas, 1 marco → thrashing extremo) |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 64 --frames 1 --serve &\n\n# Terminal 2 — cliente\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 64 --frames 1\nwait\n``` |
| **Pasos** | 1. Arrancar con 1 marco\n2. Observar: `expulsiones` muy alto, `swap_bytes` alto\n3. Verificar que **termina** (no hang infinito)\n4. `verificacion OK` o `verificacion FALLIDA` pero exit controlado |
| **Resultado Esperado** | • Termina en tiempo razonable (< 60 s)\n• `expulsiones` ≈ 64×iteraciones\n• Exit 0 o 3 (verificación fallida pero controlado)\n• No deadlock, no SIGSEGV |
| **Evidencia** | Captura final; tiempo de ejecución |
| **Criterios Aprobación** | PASS: termina, exit controlado, sin hang/crash |
| **Troubleshooting** | • **Hang:** deadlock en LRU o fault queue — revisar `fault_queue_size` y locks\n• **Muy lento:** esperado con 1 marco; reducir `--pages` si > 120 s |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-20: Evicción y recuperación**

| Elemento | Detalle |
|---|---|
| **ID** | PR-20 |
| **Nombre** | Evict + re-acceso (página vuelve de swap) |
| **Objetivo** | Verificar que una página evictada a swap puede ser recuperada (page fault → read swap → install) y el acceso continúa correcto. |
| **Tipo** | Positiva / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-17 aprobado (swap funciona) |
| **Configuración** | `--pages 128 --frames 32` (presión 4:1, genera swap) |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app evict --pages 128 --frames 32 --serve &\n\n# Terminal 2 — cliente\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app evict --pages 128 --frames 32\nwait\n``` |
| **Pasos** | 1. Ejecutar evict (genera swap en home)\n2. Verificar `swap_bytes > 0` en stats home\n3. Verificar `verificacion OK` en ambos (implica re-acceso a páginas swapeadas)\n4. `dsmctl stats` home: `page_faults`, `swap_in`, `swap_out` |
| **Resultado Esperado** | • `swap_out > 0`, `swap_in > 0` (recuperación)\n• `verificacion OK` ambos\n• `page_faults` incluye faults de swap-in |
| **Evidencia** | `dsmctl stats` con swap_in/out; capturas verificación |
| **Criterios Aprobación** | PASS: swap_in > 0, verificacion OK, exit 0 |
| **Troubleshooting** | • **swap_in = 0:** páginas no re-accedidas — patrón evict no re-lee; verificar app_evict.c\n• **Verificación falla:** datos corruptos en swap — revisar `memory.c` write/read swap |
| **Limpieza** | `pkill -f dsm_node`; `rm -f /tmp/dsm-swap-*` (si usa archivos temporales) |

---

### 3.3 Concurrencia (PR-21 a PR-25)

---

#### **PR-21: Accesos simultáneos (lectura/escritura mixtos)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-21 |
| **Nombre** | Accesos concurrentes mixtos R/W |
| **Objetivo** | Verificar coherencia bajo carga mixta: múltiples hilos leyendo y escribiendo páginas compartidas simultáneamente. |
| **Tipo** | Integración / Concurrencia |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-04, PR-14 aprobados |
| **Configuración** | `nodes_4.conf`; app `stress` (combina matrices + counter) |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app stress --serve &\n\n# Terminal 2 — nodo 1\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app stress &\n\n# Terminal 3 — nodo 2\nPC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app stress &\n\n# Terminal 4 — nodo 3\nPC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app stress\nwait\n``` |
| **Pasos** | 1. Arrancar home con `--app stress --serve`\n2. Arrancar 3 clientes con `--app stress`\n3. Stress ejecuta: matrices 192 + counter 2000×4 hilos\n4. Verificar: `matrices: OK`, `counter: suma = 32000 OK` (4 nodos × 4 hilos × 2000)\n5. `dsmctl stats`: `invalidaciones`, `fallos_pagina`, `transferencias` altos |
| **Resultado Esperado** | • 4/4 nodos: `matrices OK` + `counter OK`\n• Exit 0 todos\n• Stats muestran alta actividad coherencia |
| **Evidencia** | 4 capturas OK; `dsmctl stats` home |
| **Criterios Aprobación** | PASS: 4/4 OK, exit 0, stats coherentes |
| **Troubleshooting** | • **Counter incorrecto:** race en atomic increment — revisar `app_counter.c`\n• **Matrices discrepancias:** coherencia rota — revisar invalidaciones en `coherence.c`\n• **Timeout barrera:** aumentar `barrier_timeout_ms` |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-22: Escrituras concurrentes (counter distribuido)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-22 |
| **Nombre** | Counter distribuido (incrementos atómicos) |
| **Objetivo** | Verificar que incrementos concurrentes en la misma variable compartida no pierden actualizaciones (atomicidad + coherencia). |
| **Tipo** | Positiva / Concurrencia |
| **Topología** | **T8** (4 PCs / 4 nodos) — **prueba real de red** |
| **Requisitos Previos** | • 4 PCs configuradas (ver sección 7)\n• IPs reales en `nodes_4.conf`\n• Firewall abierto puertos 27300–27315 |
| **Configuración** | `nodes_4.conf` con IPs reales; `--iters 5000 --threads 4` |
| **Comandos EXACTOS** | ```bash\n# PC-A (home / NODE-0)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app counter --iters 5000 --threads 4 --serve &\n\n# PC-B (NODE-1)\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app counter --iters 5000 --threads 4 &\n\n# PC-C (NODE-2)\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app counter --iters 5000 --threads 4 &\n\n# PC-D (NODE-3)\nPC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app counter --iters 5000 --threads 4\nwait\n``` |
| **Pasos** | 1. En PC-A: arrancar home con `--serve`\n2. En PC-B,C,D: arrancar clientes (simultáneo o escalonado < 5 s)\n3. Esperar `wait` en PC-A\n4. Verificar en PC-A: `suma = 80000, esperado = 80000, OK` (4×4×5000)\n5. `dsmctl stats` en PC-A: `invalidaciones` muy alto (~80k) |
| **Resultado Esperado** | • Suma exacta = esperada (sin pérdidas)\n• `invalidaciones` ≈ número de escrituras\n• Latencias de red visibles en `remote_write` |
| **Evidencia** | Captura PC-A suma OK; `dsmctl stats` PC-A; logs 4 PCs |
| **Criterios Aprobación** | PASS: suma exacta, exit 0 en 4 PCs, invalidaciones > 0 |
| **Troubleshooting** | • **Suma < esperada:** actualizaciones perdidas — race en `dsm_coherence_handle_write()` o atomic\n• **Timeout barrera:** red lenta — aumentar `barrier_timeout_ms` a 60000\n• **PC no conecta:** firewall, IPs mal en `nodes_4.conf`, `seed_host` en configs |
| **Limpieza** | `pkill -f dsm_node` en las 4 PCs |

---

#### **PR-23: Lecturas compartidas (matrices distribuidas)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-23 |
| **Nombre** | Matrices distribuidas (lecturas compartidas masivas) |
| **Objetivo** | Verificar estado **S** (Shared) estable con múltiples lectores concurrentes en multiplicación de matrices por bloques. |
| **Tipo** | Positiva / Concurrencia / Rendimiento |
| **Topología** | **T8** (4 PCs / 4 nodos) |
| **Requisitos Previos** | • PR-22 aprobado (clúster 4 PCs funcional) |
| **Configuración** | `nodes_4.conf` IPs reales; `--size 128` |
| **Comandos EXACTOS** | ```bash\n# PC-A (home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 128 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 128 &\n\n# PC-C\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 128 &\n\n# PC-D\nPC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size 128\nwait\n``` |
| **Pasos** | 1. Arrancar home `--serve`\n2. Arrancar 3 clientes\n3. Cada nodo computa bloques, lee matriz A y B compartidas → estado S\n4. Verificar 4/4: `verificacion: OK (0 discrepancias)`\n5. `dsmctl stats` home: `sharers` > 1 en páginas de matrices, `invalidaciones` bajo |
| **Resultado Esperado** | • 4/4 OK\n• Páginas de matrices en estado S con 3-4 sharers\n• `invalidaciones` ≈ 0 durante computación (solo al inicio) |
| **Evidencia** | 4 capturas OK; `dsmctl pages` home mostrando estado S |
| **Criterios Aprobación** | PASS: 4/4 OK, estado S predominante, exit 0 |
| **Troubleshooting** | • **Discrepancias:** coherencia rota — invalidaciones prematuras\n• **Lento:** red — verificar `bench` latencias; loopback vs LAN |
| **Limpieza** | `pkill -f dsm_node` en 4 PCs |

---

#### **PR-24: Barreras de sincronización**

| Elemento | Detalle |
|---|---|
| **ID** | PR-24 |
| **Nombre** | Barreras distribuidas (todos los nodos) |
| **Objetivo** | Verificar que `dsm_barrier_wait()` sincroniza correctamente todos los nodos del clúster, con timeout configurable. |
| **Tipo** | Integración / Concurrencia |
| **Topología** | **T4** (1 PC / 4 nodos) o **T8** (4 PCs) |
| **Requisitos Previos** | • Clúster 4 nodos funcional (PR-04 o PR-22) |
| **Configuración** | App `demo` (usa 3 barreras) o `matrices` (barreras implícitas por fase) |
| **Comandos EXACTOS** | ```bash\n# Usando demo (3 barreras explícitas)\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app demo --serve &\n\n# Terminal 2-4 — clientes\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app demo &\nPC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app demo &\nPC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app demo\nwait\n``` |
| **Pasos** | 1. Ejecutar demo en 4 nodos\n2. Demo imprime: `barrera 1/3`, `barrera 2/3`, `barrera 3/3`\n3. Verificar que **todos** nodos imprimen las 3 barreras en orden\n4. Si un nodo tarda > `barrier_timeout_ms` (30 s), barrera falla y app aborta |
| **Resultado Esperado** | • 4/4 nodos: `barreras: 3/3 completadas`\n• Exit 0 todos\n• Sin `BARRIER_TIMEOUT` en logs |
| **Evidencia** | Capturas 4 nodos con barreras; logs sin timeout |
| **Criterios Aprobación** | PASS: 4/4 barreras 3/3, exit 0, sin timeout |
| **Troubleshooting** | • **BARRIER_TIMEOUT:** un nodo colgado — revisar `dsm_barrier_wait()` y `barrier_timeout_ms`\n• **Barrera 2/3 only:** nodo murió — revisar heartbeat y `nodes` count |
| **Limpieza** | `pkill -f dsm_node` |

---

#### **PR-25: Task distribuido (Extra 1 — ejecución distribuida)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-25 |
| **Nombre** | Task distribuido (Extra 1) |
| **Objetivo** | Verificar ejecución de tarea paralela divisible (Extra 1): coordinador reparte trabajo, trabajadores computan, coordinador agrega y valida speedup/balance. |
| **Tipo** | Integración / Extra 1 |
| **Topología** | **T8** (4 PCs / 4 nodos) — **requerido para speedup real** |
| **Requisitos Previos** | • PR-23 aprobado (4 PCs funcionando) |
| **Configuración** | `nodes_4.conf` IPs reales; `--size 1000000` (1M elementos) |
| **Comandos EXACTOS** | ```bash\n# PC-A (coordinador / home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app task --size 1000000 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app task --size 1000000 &\n\n# PC-C\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app task --size 1000000 &\n\n# PC-D\nPC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app task --size 1000000\nwait\n``` |
| **Pasos** | 1. Arrancar coordinador (home) con `--serve`\n2. Arrancar 3 trabajadores\n3. Coordinador imprime: `total paralelo = X, total secuencial = Y, speedup = Z, balance = W`\n4. Verificar: `speedup > 1.0`, `balance ≈ 1.0` (0.8-1.2 aceptable)\n5. Trabajadores imprimen su porción y tiempo |
| **Resultado Esperado** | • Coordinador: `OK` final, `speedup > 1.0`, `balance` cercano a 1.0\n• 4/4 exit 0\n• Logs muestran distribución de trabajo |
| **Evidencia** | Captura coordinador con speedup/balance; captura trabajador porción |
| **Criterios Aprobación** | PASS: speedup > 1.0, balance ∈ [0.8, 1.2], 4/4 OK, exit 0 |
| **Troubleshooting** | • **speedup ≤ 1.0:** overhead > ganancia — tamaño `--size` muy pequeño; aumentar a 10M\n• **balance << 1.0:** reparto desigual — revisar `app_task.c` particionado\n• **Timeout:** barrera en task — aumentar `barrier_timeout_ms` |
| **Limpieza** | `pkill -f dsm_node` en 4 PCs |

---

### 3.4 Pruebas Negativas (PR-26 a PR-33)

---

#### **PR-26: Configuración incorrecta (config mismatch)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-26 |
| **Nombre** | Config mismatch (huella distinta) |
| **Objetivo** | Verificar detección rápida de configuraciones incompatibles entre nodos (page_size, node_memory, etc.). |
| **Tipo** | Negativa |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02 aprobado |
| **Configuración** | Modificar `config/node1.conf`: `page_size = 8192` (vs 4096 en node0) |
| **Comandos EXACTOS** | ```bash\n# En terminal 1 — home (config correcta)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --serve &\n\n# En terminal 2 — cliente (config modificada)\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf\n``` |
| **Pasos** | 1. Editar `node1.conf`: cambiar `page_size = 8192`\n2. Arrancar home normal\n3. Arrancar cliente con config distinta\n4. Observar error de **config mismatch**\n5. Restaurar `page_size = 4096` en node1.conf |
| **Resultado Esperado** | • Error inmediato: `config mismatch: fingerprint 0x... != 0x...`\n• Exit ≠ 0 (rápido, no espera 10 s)\n• Home sigue vivo |
| **Evidencia** | Captura error mismatch; diff de configs |
| **Criterios Aprobación** | PASS: error detectado < 2 s, exit ≠ 0, mensaje claro |
| **Troubleshooting** | • **No detecta:** `dsm_config_fingerprint()` no incluye `page_size` — revisar `config.c`\n• **Tarda 10 s:** timeout de conexión — mismatch debe detectarse en handshake inicial |
| **Limpieza** | `pkill -f dsm_node`; restaurar `node1.conf` original |

---

#### **PR-27: IP/puerto inválido**

| Elemento | Detalle |
|---|---|
| **ID** | PR-27 |
| **Nombre** | IP/puerto inválido en nodes.conf |
| **Objetivo** | Verificar validación de tabla de nodos: IP malformada, puerto fuera de rango, duplicados. |
| **Tipo** | Negativa |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • Binarios listos |
| **Configuración** | Crear `config/nodes_bad.conf` con entradas inválidas |
| **Comandos EXACTOS** | ```bash\n# Caso 1: IP inválida\ncat > config/nodes_bad.conf << 'EOF'\n0 999.999.999.999 27300\nEOF\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_bad.conf\n\n# Caso 2: Puerto fuera de rango\ncat > config/nodes_bad.conf << 'EOF'\n0 127.0.0.1 99999\nEOF\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_bad.conf\n\n# Caso 3: ID duplicado\ncat > config/nodes_bad.conf << 'EOF'\n0 127.0.0.1 27300\n0 127.0.0.1 27304\nEOF\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_bad.conf\n``` |
| **Pasos** | 1. Probar cada caso\n2. Verificar error en `dsm_nodes_load()`\n3. Exit ≠ 0, mensaje descriptivo |
| **Resultado Esperado** | • Caso 1: `IP inválida`\n• Caso 2: `puerto fuera de rango`\n• Caso 3: `ID duplicado`\n• Todos: exit ≠ 0, no crash |
| **Evidencia** | 3 capturas errores |
| **Criterios Aprobación** | PASS: 3/3 errores válidos, exit ≠ 0, sin SIGSEGV |
| **Troubleshooting** | • **Crash:** validación faltante en `nodes.c` — añadir checks\n• **Exit 0:** no valida — revisar `dsm_nodes_load()` return |
| **Limpieza** | `rm config/nodes_bad.conf` |

---

#### **PR-28: Nodo ausente (no arranca)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-28 |
| **Nombre** | Nodo declarado pero no iniciado |
| **Objetivo** | Verificar comportamiento cuando `nodes.conf` declara 4 nodos pero solo 3 arrancan (el 4º nunca aparece). |
| **Tipo** | Negativa / Integración |
| **Topología** | **T4** (1 PC / 4 nodos declarados, 3 reales) |
| **Requisitos Previos** | • PR-04 aprobado |
| **Configuración** | `nodes_4.conf` (4 nodos); arrancar solo 0,1,2 |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 64 --serve &\n\n# Terminal 2 — nodo 1\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 64 &\n\n# Terminal 3 — nodo 2\nPC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 64\n# NODE-3 NUNCA ARRANCA\nwait\n``` |
| **Pasos** | 1. Arrancar 3 de 4 nodos\n2. Las apps usan barreras que esperan `nodes.count` (4) participantes\n3. Observar: barrera espera hasta `barrier_timeout_ms` (30 s)\n4. Tras timeout: app aborta con error de barrera |
| **Resultado Esperado** | • Barrera expira tras ~30 s\n• Error: `BARRIER_TIMEOUT` o `DSM_ERR_TIMEOUT`\n• Exit ≠ 0 en nodos participantes\n• No hang infinito |
| **Evidencia** | Captura timeout barrera (30 s) |
| **Criterios Aprobación** | PASS: timeout en ~30 s, exit ≠ 0, no hang |
| **Troubleshooting** | • **Hang > 60 s:** `barrier_timeout_ms` no respetado — revisar `sync.c`\n• **Exit 0:** app no verifica barrera — bug en `app_matrices.c` |
| **Limpieza** | `pkill -f dsm_node` (si quedan procesos) |

---

#### **PR-29: Nodo lento (latencia alta / pausa)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-29 |
| **Nombre** | Nodo lento / pausado (simular red lenta) |
| **Objetivo** | Verificar tolerancia a un nodo que responde lento (heartbeat tardío, RPC lentas) sin expulsarlo prematuramente. |
| **Tipo** | Negativa / Estrés |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02 aprobado |
| **Configuración** | `nodes_2.conf`; usar `tc` (traffic control) para añadir latencia 500 ms en loopback, o `SIGSTOP`/`SIGCONT` manual |
| **Comandos EXACTOS** | ```bash\n# Opción A: tc (requiere sudo)\nsudo tc qdisc add dev lo root netem delay 500ms\n\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 64 --serve &\n\n# Terminal 2 — cliente\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 64\nwait\n\n# Limpiar tc\nsudo tc qdisc del dev lo root\n``` |
| **Pasos** | 1. Añadir latencia 500 ms en loopback (`tc`)\n2. Ejecutar matrices 64\n3. Verificar que completa (más lento) pero **sin timeout de heartbeat**\n4. `heartbeat_timeout_ms = 3000` (3 s) > 500 ms latencia → OK\n5. Quitar `tc` |
| **Resultado Esperado** | • Completa OK (más lento)\n• Heartbeats llegan dentro de 3 s (500 ms RTT < 3 s)\n• Exit 0 ambos |
| **Evidencia** | Captura completando con latencia; tiempo total > normal |
| **Criterios Aprobación** | PASS: exit 0, sin heartbeat timeout, verificación OK |
| **Troubleshooting** | • **Heartbeat timeout:** `heartbeat_timeout_ms` muy bajo — aumentar a 5000\n• **Muy lento:** esperado; si > 120 s, reducir latencia tc a 100 ms |
| **Limpieza** | `sudo tc qdisc del dev lo root`; `pkill -f dsm_node` |

---

#### **PR-30: Interrupción Ctrl+C (SIGINT) durante ejecución**

| Elemento | Detalle |
|---|---|
| **ID** | PR-30 |
| **Nombre** | Ctrl+C en medio de app (shutdown grácil bajo carga) |
| **Objetivo** | Verificar que `Ctrl+C` durante una aplicación larga (matrices 192) detiene grácilmente: hilos join, puertos liberan, logs flushean. |
| **Tipo** | Negativa / Integración |
| **Topología** | **T2** (1 PC / 2 nodos) |
| **Requisitos Previos** | • PR-02, PR-11 aprobados |
| **Configuración** | `nodes_2.conf`; app `matrices --size 192` (tarda ~60 s) |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 192 --serve &\n\n# Terminal 2 — cliente\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 192 &\n\n# ESPERAR 5 SEGUNDOS\nsleep 5\n\n# Ctrl+C en home (terminal 1)\nCtrl+C\n``` |
| **Pasos** | 1. Arrancar matrices 192 en ambos\n2. Esperar 5 s (app en progreso)\n3. `Ctrl+C` en home\n4. Verificar: home imprime `deteniendo...`, cliente recibe error/termina\n5. Puertos libres en < 2 s\n6. Logs completos |
| **Resultado Esperado** | • Home: `deteniendo...`, exit 0 o 3\n• Cliente: error `PEER_DOWN` o `TIMEOUT`, exit ≠ 0\n• Puertos 27300-27307 libres\n• Logs tienen entrada final |
| **Evidencia** | Captura Ctrl+C → deteniendo; `ss -ltn` puertos libres |
| **Criterios Aprobación** | PASS: ambos terminan, puertos libres, logs completos, sin zombies |
| **Troubleshooting** | • **Cliente cuelga:** no detecta peer down — revisar `dsm_runtime_start_services()` heartbeat check\n• **Puertos ocupados:** hilos no join — revisar `dsm_runtime_destroy()` |
| **Limpieza** | `pkill -f dsm_node` (si queda algo); `ss -ltn \| grep 2730` |

---

#### **PR-31: Memoria fuera de rango (OOB access)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-31 |
| **Nombre** | Acceso fuera de rango (dsm_read/write beyond allocated) |
| **Objetivo** | Verificar que accesos inválidos a memoria DSM (offset ≥ page_size, puntero no allocado) son detectados y no corrompen estado. |
| **Tipo** | Negativa / Seguridad |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • Binarios listos<br>• Requiere modificar app o usar `dsmctl` para probar (no hay app expuesta para OOB) |
| **Configuración** | `nodes_1.conf`; test manual via `dsm_api` (no hay CLI directo) — **documentar como prueba de código** |
| **Comandos EXACTOS** | ```bash\n# No hay comando directo — esta prueba se valida por REVISIÓN DE CÓDIGO:\n# - dsm_api.c: dsm_read/dsm_write validan offset < page_size\n# - dsm_fault.c: page fault handler valida página válida\n# - memory.c: frame access valida bounds\n\necho \"PR-31: Validar en code review: bounds checks en dsm_api.c, memory.c, fault.c\"\n``` |
| **Pasos** | 1. Revisar código: `dsm_read()` linea ~XX valida `offset < cfg.page_size`\n2. `dsm_write()` misma validación\n3. `page_fault_handler()` valida `page_id < max_pages`\n4. `memory.c` frame access valida índice\n5. Confirmar que retornan `DSM_ERR_INVALID_ARG` o `SIGSEGV` controlado |
| **Resultado Esperado** | Código tiene validaciones; fuzzing/unit test pasaría |
| **Evidencia** | Referencias a líneas de código con checks |
| **Criterios Aprobación** | PASS: code review confirma bounds checks en todas las entradas públicas |
| **Troubleshooting** | • **Falta validación:** añadir en `dsm_api.c` antes de `memcpy` |
| **Limpieza** | N/A |

---

#### **PR-32: Carga excesiva (stress combinado)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-32 |
| **Nombre** | Stress combinado (matrices 192 + counter 2000×4) |
| **Objetivo** | Verificar estabilidad bajo carga máxima sostenida: CPU, memoria, red, coherencia simultáneas. |
| **Tipo** | Estrés / Rendimiento |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-17, PR-21 aprobados |
| **Configuración** | `nodes_4.conf`; app `stress` (ya combina matrices 192 + counter) |
| **Comandos EXACTOS** | ```bash\n# Terminal 1 — home\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app stress --serve &\n\n# Terminal 2-4 — clientes\nPC-A$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app stress &\nPC-A$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app stress &\nPC-A$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app stress\nwait\n``` |
| **Pasos** | 1. Arrancar stress en 4 nodos\n2. Stress ejecuta: matrices 192 (pesado) + counter 2000×4 hilos\n3. Monitorear: CPU ~100%, RAM, red loopback\n4. Verificar: 4/4 `matrices OK` + `counter OK`\n5. Tiempo total ~60-120 s\n6. `dsmctl stats` final: contadores máximos |
| **Resultado Esperado** | • 4/4 nodos: ambas apps OK\n• Exit 0 todos\n• Sin crash, OOM, deadlock\n• Stats: `fallos_pagina` > 10k, `invalidaciones` > 50k |
| **Evidencia** | 4 capturas OK; `dsmctl stats` home; `top` screenshot CPU/RAM |
| **Criterios Aprobación** | PASS: 4/4 OK, exit 0, recursos estables, sin errores |
| **Troubleshooting** | • **OOM:** `ulimit -v` o reducir `node_memory`\n• **Deadlock:** revisar `fault_queue_size` y thread pool\n• **Timeout barrera:** `barrier_timeout_ms = 60000` |
| **Limpieza** | `pkill -f dsm_node`; `sync` |

---

#### **PR-33: Recursos agotados (file descriptors, memoria, hilos)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-33 |
| **Nombre** | Agotamiento recursos sistema (FD, threads, RAM) |
| **Objetivo** | Verificar comportamiento al límite de descriptors (sockets + epoll), hilos (thread_pool=32 × 4 nodos = 128), y RAM. |
| **Tipo** | Estrés / Límites |
| **Topología** | **T4** (1 PC / 4 nodos) |
| **Requisitos Previos** | • PR-32 aprobado |
| **Configuración** | `nodes_4.conf`; `thread_pool_size = 32` (default); `max_message_size = 8192`; `fault_queue_size = 1024` |
| **Comandos EXACTOS** | ```bash\n# Verificar límites antes\nPC-A$ ulimit -n          # file descriptors (default 1024)\nPC-A$ ulimit -u          # max user processes/threads\n\n# Ejecutar stress (ya probado en PR-32)\n# Si pasa PR-32, recursos son suficientes para carga nominal\n\n# Test extra: arrancar 8 nodos (duplicar) para forzar límites\n# Crear nodes_8.conf con puertos 27300-27331\n# node4.conf..node7.conf con puertos 27316-27331\n# Arrancar 8 nodos con stress\n``` |
| **Pasos** | 1. Verificar `ulimit -n` ≥ 2048 (recomendado)\n2. Si PR-32 pasa con 4 nodos, recursos OK para especificación\n3. Opcional: probar 8 nodos para encontrar límite real |
| **Resultado Esperado** | • 4 nodos stress: dentro de límites\n• `ulimit -n` 1024 → 4 nodos × ~50 FD = 200 OK\n• Hilos: 4 × 32 = 128 < `ulimit -u` (típico 4096+)\n• RAM: 4 × 1 MB + overhead < 100 MB |
| **Evidencia** | `ulimit -a` output; `top` durante stress 4 nodos |
| **Criterios Aprobación** | PASS: 4 nodos stress dentro de límites; no `EMFILE`/`EAGAIN` |
| **Troubleshooting** | • **EMFILE:** `ulimit -n 4096`\n• **EAGAIN threads:** `ulimit -u 8192`\n• **OOM:** reducir `node_memory` o `thread_pool_size` |
| **Limpieza** | `pkill -f dsm_node` |

---

### 3.5 Rendimiento (PR-34 a PR-40)

---

#### **PR-34: Baseline 1 PC (matrices 128)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-34 |
| **Nombre** | Baseline secuencial 1 PC (referencia) |
| **Objetivo** | Establecer referencia de tiempo y métricas en loopback 1 nodo (sin coherencia distribuida). |
| **Tipo** | Rendimiento / Baseline |
| **Topología** | **T1** (1 PC / 1 nodo) |
| **Requisitos Previos** | • PR-01 aprobado |
| **Configuración** | `nodes_1.conf`; `--app matrices --size 128` |
| **Comandos EXACTOS** | ```bash\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_1.conf --app matrices --size 128\n``` |
| **Pasos** | 1. Ejecutar matrices 128 en 1 nodo\n2. Anotar: `tiempo total`, `verificacion OK`\n3. `dsmctl stats`: `fallos_pagina`, `transferencias` (deben ser 0 o mínimos)\n4. Repetir 3 veces, promediar |
| **Resultado Esperado** | • `verificacion OK`\n• Tiempo: ~X s (registrar)\n• `transferencias = 0` (sin red)\n• `fallos_pagina` = páginas tocadas |
| **Evidencia** | 3 ejecuciones con tiempos; stats |
| **Criterios Aprobación** | PASS: 3/3 OK, tiempos consistentes (CV < 10%) |
| **Troubleshooting** | • **Variabilidad alta:** GC, swap, otros procesos — cerrar apps, `sync; echo 3 > /proc/sys/vm/drop_caches` (root) |
| **Limpieza** | N/A |

---

#### **PR-35: 2 PCs (matrices 128 / 192)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-35 |
| **Nombre** | 2 PCs físicas — matrices 128 y 192 |
| **Objetivo** | Medir overhead de red real vs loopback: latencia, throughput, speedup. |
| **Tipo** | Rendimiento / Integración |
| **Topología** | **T5** (2 PCs / 2 nodos) |
| **Requisitos Previos** | • 2 PCs configuradas (ver sección 7)\n• `nodes_2.conf` con IPs reales\n• Firewall abierto\n• PR-22 aprobado (counter 2 PCs OK) |
| **Configuración** | PC-A (home): `node0.conf` con `bind_address=0.0.0.0`, `seed_host=IP_A`<br>PC-B: `node1.conf` con `seed_host=IP_A` |
| **Comandos EXACTOS** | ```bash\n# Matrices 128\n# PC-A (home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 128 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 128\nwait\n\n# Matrices 192 (más carga)\n# PC-A\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 192 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 192\nwait\n``` |
| **Pasos** | 1. Ejecutar matrices 128 en 2 PCs\n2. Registrar tiempo PC-A (coordinador)\n3. Ejecutar matrices 192\n4. Registrar tiempo\n4. Comparar con PR-34 (baseline 1 PC)\n5. `dsmctl stats`: `remote_read`, `remote_write`, `latencia_red` |
| **Resultado Esperado** | • 2/2 OK ambas tamaños\n• Tiempo 2 PCs > 1 PC (overhead red)\n• Speedup < 1 (esperado para matrices 128 en 2 PCs por overhead)\n• `bench` latencias: `remote_read` > `local_read` × 10-100 (LAN vs loopback) |
| **Evidencia** | Tabla comparativa: 1 PC vs 2 PCs (tiempo, speedup); `dsmctl stats`; `bench` output |
| **Criterios Aprobación** | PASS: 2/2 OK, métricas registradas, speedup calculado |
| **Troubleshooting** | • **Muy lento (> 5× baseline):** red congestionada, MTU, firewall throttling\n• **Timeout barrera:** `barrier_timeout_ms = 60000`\n• **Checksum mismatch:** coherencia rota en red real — revisar `protocol.c` reordenamiento |
| **Limpieza** | `pkill -f dsm_node` en ambas PCs |

---

#### **PR-36: 3 PCs (matrices 128)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-36 |
| **Nombre** | 3 PCs físicas — escalabilidad |
| **Objetivo** | Verificar escalabilidad a 3 nodos físicos: speedup, balance, coherencia. |
| **Tipo** | Rendimiento / Integración |
| **Topología** | **T7** (3 PCs / 3 nodos) |
| **Requisitos Previos** | • 3 PCs configuradas\n• `nodes_3.conf` con IPs reales\n• PR-35 aprobado |
| **Configuración** | `nodes_3.conf`: 3 entradas con IPs reales; PC-A home, PC-B/C clientes |
| **Comandos EXACTOS** | ```bash\n# PC-A (home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_3.conf --app matrices --size 128 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_3.conf --app matrices --size 128 &\n\n# PC-C\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_3.conf --app matrices --size 128\nwait\n``` |
| **Pasos** | 1. Arrancar 3 PCs\n2. Matrices 128\n3. Registrar tiempo, speedup vs 1 PC y vs 2 PCs\n4. `dsmctl stats`: invalidaciones, transferencias, balance de carga |
| **Resultado Esperado** | • 3/3 OK\n• Speedup 3 PCs > Speedup 2 PCs (escalabilidad)\n• Balance ≈ 1.0 (carga repartida equitativamente) |
| **Evidencia** | Tabla 1/2/3 PCs; stats 3 PCs |
| **Criterios Aprobación** | PASS: 3/3 OK, speedup creciente, balance ∈ [0.8, 1.2] |
| **Troubleshooting** | • **Speedup no crece:** overhead coordinación > ganancia — tamaño matriz pequeño\n• **Balance malo:** particionado estático — revisar `app_matrices.c` distribución bloques |
| **Limpieza** | `pkill -f dsm_node` en 3 PCs |

---

#### **PR-37: 4 PCs (matrices 128)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-37 |
| **Nombre** | 4 PCs físicas — clúster completo |
| **Objetivo** | Verificar operación en topología máxima especificada (4 PCs / 4 nodos). |
| **Tipo** | Rendimiento / Integración / Final |
| **Topología** | **T8** (4 PCs / 4 nodos) |
| **Requisitos Previos** | • 4 PCs configuradas (sección 7)\n• `nodes_4.conf` IPs reales\n• PR-36 aprobado |
| **Configuración** | `nodes_4.conf` IPs reales; todos los configs `seed_host=IP_A` |
| **Comandos EXACTOS** | ```bash\n# PC-A (home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 128 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 128 &\n\n# PC-C\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 128 &\n\n# PC-D\nPC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size 128\nwait\n``` |
| **Pasos** | 1. Arrancar 4 PCs simultáneamente (ventana < 10 s)\n2. Matrices 128\n3. Registrar: tiempo, speedup vs 1/2/3 PCs, balance\n4. `dsmctl stats` completo\n5. `dsmctl dir` en home: directorio global 4 nodos |
| **Resultado Esperado** | • 4/4 OK\n• Speedup máximo del proyecto\n• Balance ∈ [0.8, 1.2]\n• Directorio: 4 nodos `alive=1`, `last_seen` reciente |
| **Evidencia** | Tabla comparativa 1-4 PCs; `dsmctl dir`; stats; capturas 4 PCs |
| **Criterios Aprobación** | PASS: 4/4 OK, speedup registrado, balance bueno, directorio completo |
| **Troubleshooting** | • **Un nodo no conecta:** firewall, IP errónea en `nodes_4.conf`, `seed_host`\n• **Timeout:** `barrier_timeout_ms = 60000`\n• **Verificación falla:** coherencia en 4 PCs — revisar invalidaciones broadcast |
| **Limpieza** | `pkill -f dsm_node` en 4 PCs |

---

#### **PR-38: Diferentes tamaños de trabajo**

| Elemento | Detalle |
|---|---|
| **ID** | PR-38 |
| **Nombre** | Escalabilidad por tamaño de problema (weak/strong scaling) |
| **Objetivo** | Medir comportamiento con matrices 64, 128, 192, 256 en topología fija (4 PCs). |
| **Tipo** | Rendimiento / Análisis |
| **Topología** | **T8** (4 PCs / 4 nodos) |
| **Requisitos Previos** | • PR-37 aprobado |
| **Configuración** | `nodes_4.conf` IPs reales; `--size 64, 128, 192, 256` |
| **Comandos EXACTOS** | ```bash\nfor SZ in 64 128 192 256; do\n  echo \"=== Size $SZ ===\"\n  # PC-A\n  PC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size $SZ --serve &\n  # PC-B\n  PC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size $SZ &\n  # PC-C\n  PC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size $SZ &\n  # PC-D\n  PC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size $SZ\n  wait\n  sleep 5\n\ndone\n``` |
| **Pasos** | 1. Ejecutar 4 tamaños en secuencia\n2. Registrar tiempo cada uno\n3. Calcular: eficiencia paralela, overhead comunicación\n4. Graficar: tiempo vs tamaño, speedup vs tamaño |
| **Resultado Esperado** | • 4/4 tamaños OK\n• Tiempo crece ~O(n³) para matrices\n• Speedup mejora con tamaño mayor (amortiza overhead)\n• Overhead % disminuye con tamaño |
| **Evidencia** | Tabla 4 tamaños; gráficas (opcional) |
| **Criterios Aprobación** | PASS: 4/4 OK, datos registrados, tendencia clara |
| **Troubleshooting** | • **256 falla:** memoria insuficiente — `node_memory` o `frames` límite\n• **Tiempo anómalo:** GC, swap — `drop_caches` entre runs |
| **Limpieza** | `pkill -f dsm_node` entre tamaños |

---

#### **PR-39: Comparación secuencial vs distribuido**

| Elemento | Detalle |
|---|---|
| **ID** | PR-39 |
| **Nombre** | Secuencial (1 hilo) vs Distribuido (4 nodos × 4 hilos) |
| **Objetivo** | Comparar ejecución puramente secuencial (app `matrices` en 1 nodo sin DSM) vs distribuida completa. |
| **Tipo** | Rendimiento / Análisis |
| **Topología** | **T1** (secuencial) vs **T8** (distribuido 4 PCs) |
| **Requisitos Previos** | • PR-34 (secuencial) y PR-37 (distribuido) aprobados |
| **Configuración** | Secuencial: `nodes_1.conf`, 1 hilo\nDistribuido: `nodes_4.conf` IPs reales, 4 nodos × 4 hilos |
| **Comandos EXACTOS** | ```bash\n# Secuencial (ya en PR-34)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_1.conf --app matrices --size 128\n\n# Distribuido (ya en PR-37)\n# 4 PCs con matrices --size 128\n``` |
| **Pasos** | 1. Tomar tiempo secuencial de PR-34 (promedio 3 runs)\n2. Tomar tiempo distribuido de PR-37 (promedio 3 runs)\n3. Calcular: `speedup = T_seq / T_dist`\n4. Calcular: `eficiencia = speedup / 4` (4 nodos)\n5. Analizar: overhead comunicación, balance, ley de Amdahl |
| **Resultado Esperado** | • Speedup > 1 (ganancia neta)\n• Eficiencia 50-80% típico para DSM\n• Documentar: cuellos de botella (barreras, coherencia, red) |
| **Evidencia** | Tabla: T_seq, T_dist, speedup, eficiencia; análisis escrito |
| **Criterios Aprobación** | PASS: speedup > 1, eficiencia documentada, análisis incluido |
| **Troubleshooting** | • **Speedup < 1:** overhead > ganancia — matriz muy pequeña, red lenta\n• **Eficiencia < 30%:** revisar balance, barreras, invalidaciones excesivas |
| **Limpieza** | N/A (análisis post-mortem) |

---

#### **PR-40: Métricas completas (latencia/throughput/transferencias/memoria)**

| Elemento | Detalle |
|---|---|
| **ID** | PR-40 |
| **Nombre** | Perfil completo de métricas (benchmark) |
| **Objetivo** | Capturar métricas definitivas del sistema en clúster 4 PCs para informe final. |
| **Tipo** | Rendimiento / Entregable |
| **Topología** | **T8** (4 PCs / 4 nodos) |
| **Requisitos Previos** | • PR-37 aprobado\n• `bench` app funcional |
| **Configuración** | `nodes_4.conf` IPs reales; `--app bench --iters 5000` |
| **Comandos EXACTOS** | ```bash\n# PC-A (home)\nPC-A$ ./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app bench --iters 5000 --serve &\n\n# PC-B\nPC-B$ ./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app bench --iters 5000 &\n\n# PC-C\nPC-C$ ./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app bench --iters 5000 &\n\n# PC-D\nPC-D$ ./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app bench --iters 5000\nwait\n\n# Estadísticas finales en cada PC\nPC-A$ ./dsmctl --config config/node0.conf stats\nPC-B$ ./dsmctl --config config/node1.conf stats\nPC-C$ ./dsmctl --config config/node2.conf stats\nPC-D$ ./dsmctl --config config/node3.conf stats\n``` |
| **Pasos** | 1. Ejecutar bench 5000 iters en 4 PCs\n2. Bench imprime: `local_read_us`, `remote_read_us`, `write_us`, `throughput_MBps`\n3. `dsmctl stats` en cada nodo: contadores completos\n4. `dsmctl pages` en home: distribución páginas\n5. Panel web: `http://IP_A:27303/stats.json` (JSON completo) |
| **Resultado Esperado** | Métricas por nodo:\n- Latencia: local_read, remote_read, write (µs/op)\n- Throughput: MB/s\n- Transferencias: páginas movidas, invalidaciones, ownership changes\n- Memoria: residentes, libres, swap_in, swap_out, fault_rate\n- Red: bytes enviados/recibidos, retransmisiones |
| **Evidencia** | **OBLIGATORIO para entregable:**\n- 4 capturas `bench` output\n- 4 `dsmctl stats`\n- 1 `dsmctl pages` home\n- 1 `stats.json` web\n- Tabla consolidada en informe |
| **Criterios Aprobación** | PASS: todas las métricas capturadas, consistentes entre nodos, JSON válido |
| **Troubleshooting** | • **bench no imprime latencias:** app no compilada — verificar `app_bench.c` en Makefile\n• **stats.json vacío:** `web_enabled=1` en config, `web_port` libre\n• **Métricas dispares:** reloj desincronizado — NTP en todas las PCs |
| **Limpieza** | `pkill -f dsm_node` en 4 PCs |

---

## 4. Tabla de Registro de Resultados

**Instrucciones:** Copiar esta tabla al inicio de la sesión de laboratorio. Marcar cada prueba conforme se ejecuta.

| ID | Topología | Resultado | Tiempo | Errores | Evidencia | Observaciones |
|---|---|---|---|---|---|---|
| PR-01 | T1 | PENDIENTE | - | - | - | - |
| PR-02 | T2 | PENDIENTE | - | - | - | - |
| PR-03 | T3 | PENDIENTE | - | - | - | - |
| PR-04 | T4 | PENDIENTE | - | - | - | - |
| PR-05 | T4 | PENDIENTE | - | - | - | - |
| PR-06 | T4 | PENDIENTE | - | - | - | - |
| PR-07 | T2 | PENDIENTE | - | - | - | - |
| PR-08 | T2 | PENDIENTE | - | - | - | - |
| PR-09 | T1 | PENDIENTE | - | - | - | - |
| PR-10 | T4 | PENDIENTE | - | - | - | - |
| PR-11 | T2 | PENDIENTE | - | - | - | - |
| PR-12 | T2 | PENDIENTE | - | - | - | - |
| PR-13 | T4 | PENDIENTE | - | - | - | - |
| PR-14 | T3 | PENDIENTE | - | - | - | - |
| PR-15 | T2 | PENDIENTE | - | - | - | - |
| PR-16 | T2 | PENDIENTE | - | - | - | - |
| PR-17 | T4 | PENDIENTE | - | - | - | - |
| PR-18 | T1 | PENDIENTE | - | - | - | - |
| PR-19 | T2 | PENDIENTE | - | - | - | - |
| PR-20 | T2 | PENDIENTE | - | - | - | - |
| PR-21 | T4 | PENDIENTE | - | - | - | - |
| PR-22 | T8 | PENDIENTE | - | - | - | - |
| PR-23 | T8 | PENDIENTE | - | - | - | - |
| PR-24 | T4/T8 | PENDIENTE | - | - | - | - |
| PR-25 | T8 | PENDIENTE | - | - | - | - |
| PR-26 | T2 | PENDIENTE | - | - | - | - |
| PR-27 | T1 | PENDIENTE | - | - | - | - |
| PR-28 | T4 | PENDIENTE | - | - | - | - |
| PR-29 | T2 | PENDIENTE | - | - | - | - |
| PR-30 | T2 | PENDIENTE | - | - | - | - |
| PR-31 | T1 | PENDIENTE | - | - | - | (code review) |
| PR-32 | T4 | PENDIENTE | - | - | - | - |
| PR-33 | T4 | PENDIENTE | - | - | - | - |
| PR-34 | T1 | PENDIENTE | - | - | - | - |
| PR-35 | T5 | PENDIENTE | - | - | - | - |
| PR-36 | T7 | PENDIENTE | - | - | - | - |
| PR-37 | T8 | PENDIENTE | - | - | - | - |
| PR-38 | T8 | PENDIENTE | - | - | - | - |
| PR-39 | T1/T8 | PENDIENTE | - | - | - | - |
| PR-40 | T8 | PENDIENTE | - | - | - | - |

**Estados:** `PENDIENTE` | `APROBADA` | `FALLIDA` | `BLOQUEADA` | `NO_APLICABLE`

---

## 5. Diagnóstico de Fallos Comunes

| Síntoma | Causa Probable | Solución |
|---|---|---|
| `bind: Address already in use` | Proceso previo no liberó puerto | `pkill -f dsm_node`; `fuser -k 27300/tcp ...`; `ss -ltnp` |
| `config mismatch: fingerprint 0xA != 0xB` | Configs difieren (page_size, node_memory, etc.) | `./dsm_node --check-config` en todos; igualar `.conf` |
| `connection refused` / `timeout` | Home no arrancado, IP/puerto mal en `nodes.conf`, firewall | Verificar `seed_host/port` en cliente; `nc -zv IP PORT`; firewall |
| `BARRIER_TIMEOUT` | Un nodo colgado/muerto; `barrier_timeout_ms` muy bajo | Aumentar `barrier_timeout_ms=60000`; revisar heartbeat |
| `verificacion: FALLIDA (X discrepancias)` | Coherencia rota (invalidaciones perdidas, race) | Revisar `coherence.c`, `protocol.c`; logs DEBUG (`log_level=2`) |
| `counter: suma=7998 esperado=8000` | Actualizaciones perdidas (atomicidad) | `dsm_coherence_handle_write()` debe ser atómico; revisar `atomic_fetch_add` |
| `dsm_alloc: sin marcos` | `node_memory` muy bajo o `--frames` muy bajo | Aumentar `node_memory` o `--frames`; reducir working set |
| `expulsiones` no aumenta / `swap_bytes=0` | `eviction_policy` no implementado o LRU roto | Revisar `memory.c` eviction; `log_level=2` para trazas |
| `stats.json` vacío / web no carga | `web_enabled=0` o puerto ocupado | `web_enabled=1` en config; `ss -ltn \| grep 27303` |
| `dsmctl: connection refused` | Puerto control erróneo o nodo no corriendo | Verificar `control_port` en config; `./dsmctl --host IP --port PORT` |
| `Huellas distintas en PCs` | Configs generadas por `dsm.sh` con IPs distintas | Ejecutar `dsm.sh` opción 1 en PC-A, copiar `config/` a otras PCs |
| `Speedup < 1` en 2+ PCs | Overhead red + coherencia > ganancia paralela | Aumentar `--size`; `bench` para medir latencia red real |
| `Segmentation fault` | Bug en `memory.c`, `fault.c`, `coherence.c` | Compilar con `-fsanitize=address -g`; `gdb` + core dump |
| `Deadlock` (hang sin CPU) | Lock ordering en `coherence.c`/`memory.c`/`fault.c` | `pstack <PID>` o `gdb` attach; revisar `pthread_mutex` orden |

---

## 6. Condiciones de Laboratorio

### 6.1 Red
- **Topología:** 4 PCs conectadas al mismo switch/red LAN (1 Gbps mínimo)
- **IPs:** Estáticas o DHCP con reservas; anotar IPs reales
- **Puertos:** 27300–27315 TCP abiertos en todas las PCs (entrada/salida)
- **Latencia:** < 1 ms LAN típica; medir con `ping` y `bench`
- **MTU:** 1500 estándar (jumbo frames 9000 opcional)

### 6.2 Firewall
```bash
# En cada PC (Linux Mint/Ubuntu/Debian):
sudo ufw allow 27300:27315/tcp
sudo ufw reload
# Verificar:
sudo ufw status numbered
```

### 6.3 Hardware Mínimo por PC
| Recurso | Mínimo | Recomendado |
|---|---|---|
| CPU | 2 cores | 4+ cores |
| RAM | 4 GB | 8+ GB |
| Disco | 2 GB libres | SSD 10+ GB libres |
| Red | 100 Mbps | 1 Gbps |

### 6.4 Sistema Operativo
- Linux Mint 21+ / Ubuntu 22.04+ / Debian 12+
- Kernel 5.15+ (support `userfaultfd` si se usa)
- `glibc` 2.35+
- `gcc` 11+ (C11)

### 6.5 Sincronía de Relojes (NTP)
```bash
# En todas las PCs (importante para métricas y timeouts):
sudo apt install ntpsec -y
sudo systemctl enable --now ntpsec
# Verificar:
ntpsec-ctl status
# Offset < 10 ms entre PCs
```

### 6.6 Dependencias de Compilación
```bash
sudo apt install build-essential gcc make git -y
# No se requieren librerías extra (solo pthread, rt, m estándar)
```

---

## 7. Checklist Pre-Laboratorio

**Ejecutar en ORDEN antes de ir al lab. Marcar ✓ al completar.**

### 7.1 En PC de desarrollo (casa/oficina)
- [ ] Clonar repo: `git clone https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git`
- [ ] `cd ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida/Entrega_Final/codigo`
- [ ] Compilar: `./build.sh` → `ls -la dsm_node dsmctl` (ambos binarios presentes)
- [ ] Verificar ayuda: `./dsm_node --help` | `./dsmctl --help`
- [ ] Crear `config/nodes_3.conf` (3 nodos loopback):
  ```bash
  cat > config/nodes_3.conf << 'EOF'
  0 127.0.0.1 27300
  1 127.0.0.1 27304
  2 127.0.0.1 27308
  EOF
  ```
- [ ] Probar PR-01 a PR-04 en local (loopback 1-4 nodos)
- [ ] Copiar carpeta `Entrega_Final/codigo/` a USB o repo para llevar a 4 PCs

### 7.2 En cada PC del laboratorio (A, B, C, D)
- [ ] Copiar `codigo/` a `~/dsm-lab/`
- [ ] `cd ~/dsm-lab`
- [ ] Compilar: `./build.sh` (verificar mismo commit: `git log --oneline -1`)
- [ ] `mkdir -p logs`
- [ ] Verificar `./dsm_node --version` → `dsm_node 2.0`

### 7.3 Configuración de Topología (solo en PC-A = home)
- [ ] `./dsm.sh`
- [ ] Opción 1 → Topología: **4** (4 PCs)
- [ ] Ingresar IPs reales:
  - `IP_A` = IP de PC-A (ej. `192.168.1.10`)
  - `IP_B` = IP de PC-B
  - `IP_C` = IP de PC-C
  - `IP_D` = IP de PC-D
- [ ] Opción 2 → Rol: **A** (home)
- [ ] Opción 11 → Validar → **Captura 0: huella OK**
- [ ] Copiar `config/` generado a PC-B, C, D:
  ```bash
  scp -r config/ user@IP_B:~/dsm-lab/
  scp -r config/ user@IP_C:~/dsm-lab/
  scp -r config/ user@IP_D:~/dsm-lab/
  ```

### 7.4 Configurar Rol en Cada PC
| PC | Comando |
|---|---|
| PC-A | `./dsm.sh` → Opción 2 → **A** |
| PC-B | `./dsm.sh` → Opción 2 → **B** |
| PC-C | `./dsm.sh` → Opción 2 → **C** |
| PC-D | `./dsm.sh` → Opción 2 → **D** |

- [ ] En **cada PC**: Opción 11 → Validar → **Huella idéntica en las 4**

### 7.5 Conectividad y Firewall
- [ ] `ping -c 3 IP_B` `IP_C` `IP_D` desde PC-A (y viceversa)
- [ ] `nc -zv IP_B 27300 27304 27308 27312` (puertos de todos los nodos)
- [ ] `sudo ufw allow 27300:27315/tcp` en **las 4 PCs**
- [ ] `ntpsec-ctl status` → offset < 10 ms entre todas

### 7.6 Prueba de Humo Rápida (2 min)
- [ ] PC-A: `./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app demo --serve &`
- [ ] PC-B: `./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app demo &`
- [ ] PC-C: `./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app demo &`
- [ ] PC-D: `./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app demo`
- [ ] Verificar: 4 PCs imprimen página compartida + `barreras: 3/3`
- [ ] `Ctrl+C` en PC-A; `pkill -f dsm_node` en todas

### 7.7 Listo para Pruebas Formales
- [ ] Tabla de registro (sección 4) impresa o en pantalla
- [ ] Cronómetro / timer app
- [ ] Cámara / smartphone para capturas de pantalla
- [ ] Carpeta `evidencia/` creada en PC-A para centralizar capturas

---

## 8. Convenciones de Evidencia

| Tipo | Formato | Naming | Ubicación |
|---|---|---|---|
| Captura terminal | PNG/JPG | `PR-XX_topologia_descripcion.png` | `evidencia/` |
| Log nodo | `.log` | `logs/dsm-nodeN.log` (auto) | `logs/` |
| Stats CLI | Texto | `PR-XX_stats_nodeN.txt` | `evidencia/` |
| Stats Web | JSON | `PR-XX_stats.json` | `evidencia/` |
| Tabla resultados | Markdown | `RESULTADOS_LAB.md` | `evidencia/` |

**Mínimo por prueba aprobada:** 1 captura terminal + log relevante + entrada en tabla registro.

---

## 9. Referencias Rápidas de Comandos

### Compilación
```bash
cd Entrega_Final/codigo
./build.sh                    # Compila dsm_node y dsmctl
make clean && make            # Alternativo (Makefile)
```

### Arranque Nodos (patrón base)
```bash
# Home (nodo 0) — siempre con --serve para pruebas multi-nodo
./dsm_node --config config/node0.conf --nodes config/nodes_X.conf --serve

# Clientes (nodo 1,2,3) — sin --serve (ejecutan app y terminan)
./dsm_node --config config/nodeN.conf --nodes config/nodes_X.conf [--app APP [ARGS]]
```

### Aplicaciones Disponibles
| App | Parámetros | Descripción |
|---|---|---|
| `demo` | -- | Página compartida + 3 barreras (humo) |
| `matrices` | `--size N` (def 64) | Multiplicación matrices por bloques |
| `file` | `--file PATH` (def sintético 1MB) | Checksum distribuido |
| `counter` | `--iters N` (100) `--threads N` (2) | Incrementos concurrentes |
| `bench` | `--iters N` (1000) `--pages N` | Latencia/throughput páginas |
| `evict` | `--pages N` (64) `--frames N` (32) | Presión memoria LRU + swap |
| `chat` | `--nick STR` `--duration S` (0=∞) | Chat tiempo real |
| `task` | `--size N` (1M) | Extra 1: tarea distribuida |
| `stress` | -- | Matrices 192 + Counter 2000×4 |

### Diagnóstico (dsmctl)
```bash
./dsmctl --config config/node0.conf stats      # Contadores
./dsmctl --config config/node0.conf pages      # Tabla páginas
./dsmctl --config config/node0.conf dir        # Directorio global (solo home)
./dsmctl --config config/node0.conf nodes      # Nodos vivos
./dsmctl --config config/node0.conf config     # Huella config
./dsmctl --host IP_A --port 27301 stats        # Remoto (desde otra PC)
```

### Web Panel
```
http://IP_A:27303/         # HTML dashboard
http://IP_A:27303/stats.json  # JSON métricas
```

### Limpieza
```bash
pkill -f dsm_node           # Mata todos los nodos
ss -ltn | grep 2730         # Verificar puertos libres
rm -f logs/*.log            # Limpiar logs (opcional)
```

---

## 10. Notas Finales

1. **Orden de ejecución recomendado:** PR-01 → PR-40 en secuencia (cada prueba valida base para la siguiente).
2. **Tiempos estimados:** ~4-6 horas para suite completa en 4 PCs (incluyendo setup, troubleshooting, evidencias).
3. **Paralelización:** PR-01 a PR-20 pueden hacerse en 1 PC (loopback) en paralelo por distintos integrantes; PR-22 a PR-40 requieren 2-4 PCs físicas.
4. **Registro:** Anotar **inmediatamente** resultado en tabla (sección 4) — no confiar en memoria.
5. **Entregable:** Al final, consolidar `evidencia/` + `RESULTADOS_LAB.md` (tabla rellenada) + análisis PR-39/PR-40 en informe final.

---

*Fin de la Guía de Pruebas de Laboratorio DSM — Versión 1.0 — Lista para ejecutar.*