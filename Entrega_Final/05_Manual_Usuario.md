# Manual de usuario — Sistema DSM (Entrega Final)

**Proyecto:** Sistema de Memoria Compartida Distribuida (DSM) con paginación bajo demanda y coherencia MSI
**Curso:** IC-6600 Principios de Sistemas Operativos
**Entregable:** Entrega Final — Demo en vivo
**Interfaz principal:** `./dsm.sh` (menú interactivo). Alternativa para WSL/loopback: `lab.sh`.
**Versión:** 1.0 (20 de noviembre de 2026)

---

## 1. Requisitos

- **Sistema operativo:** Linux (probado en Linux Mint 22.3 y WSL2; Ubuntu 24.04 y Debian 12 también funcionan). macOS no está soportado.
- **Compilador:** `gcc` con soporte C11 (`gcc --version`).
- **Librerías:** `pthreads`, `librt`, `libm` (incluidas en glibc; en Alpine instalar `build-base`).
- **Red:** una sola máquina basta para WSL/loopback; 2 PCs mínimo para el laboratorio; hasta 4 PCs para la demo final.
- **Herramientas opcionales:** `ss` (iproute2), `nc`, `curl`, `make`.

Verificación rápida:

```bash
gcc --version
make --version
ss -V
curl --version | head -1
```

---

## 2. Obtener y preparar el proyecto

```bash
git clone https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git
cd ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida/Entregable_3/prototipo2
chmod +x dsm.sh lab.sh build.sh
```

No edite los archivos `config/*.conf` a mano: `./dsm.sh` (opción 1) los actualiza y mantiene consistentes entre PCs.

---

## 3. Menú principal (`./dsm.sh`)

Al ejecutar `./dsm.sh` aparece un menú con 11 opciones:

| Opción | Acción |
|---|---|
| 1 | Configurar IPs y topología (2, 3 o 4 PCs). |
| 2 | Elegir rol de esta máquina (A/B/C/D). |
| 3 | Compilar `dsm_node` y `dsmctl`. |
| 4 | Arrancar el nodo escuchando. |
| 5 | Arrancar el nodo con una aplicación. |
| 6 | Consultar estado (nodos, stats, páginas, objetos, directorio global). |
| 7 | Ver el log de este nodo. |
| 8 | Detener los nodos (SIGTERM → SIGKILL, verifica el puerto). |
| 9 | Diagnóstico de red (IPs, procesos, puertos con PID/proceso, alcance al peer). |
| 10 | Forzar liberación de puertos (SIGKILL directo). |
| 0 | Salir. |

El estado (rol, IPs, topología) se guarda en `.dsm_state` (ignorado por Git) y se carga al volver a abrir el menú.

---

## 4. Topología y puertos

| Rol | Nodo | Puertos | Config |
|---|---|---|---|
| A | 0 (home) | 27300–27303 | `config/node0.conf` |
| B | 1 | 27304–27307 | `config/node1.conf` |
| C | 2 | 27308–27311 | `config/node2.conf` |
| D | 3 | 27312–27315 | `config/node3.conf` |

Fórmula: `base = 27300 + 4 * node_id`. El rango total de la pareja #3 es `27300–27399`.

Tablas de nodos:

- `config/nodes.conf`: 4 nodos (loopback).
- `config/nodes_2.conf`: 2 nodos (estable para `demo`, `file`, `counter`, `bench`, `evict`).
- `config/nodes_3.conf`: 3 nodos.
- `config/nodes_4.conf`: 4 nodos.

La barrera de cada aplicación cuenta los nodos declarados en el archivo que se pasa con `--nodes`.

---

## 5. Flujo de trabajo en el laboratorio

### 5.1 Configurar IPs y topología (opción 1)

En **todas las PCs**, con los mismos valores. Ejemplo para 4 PCs (usar las IP reales del laboratorio; las de la tabla son de ejemplo, RFC 5737):

```
Topología: 4
IP de la máquina A (home, nodo 0): 192.0.2.10
IP de la máquina B (cliente, nodo 1): 192.0.2.11
IP de la máquina C (cliente, nodo 2): 192.0.2.12
IP de la máquina D (cliente, nodo 3): 192.0.2.13
```

Esto regenera `config/nodes_2.conf`, `nodes_3.conf`, `nodes_4.conf` y actualiza el `seed_host` de los nodos 1–3 al IP de A.

### 5.2 Elegir rol (opción 2)

- PC A: rol **A** (home / nodo 0).
- PC B: rol **B** (cliente / nodo 1).
- PC C: rol **C** (cliente / nodo 2).
- PC D: rol **D** (cliente / nodo 3).

### 5.3 Compilar (opción 3)

Genera `dsm_node` y `dsmctl`. Si falla, revise que `gcc` y las cabeceras estén instaladas.

### 5.4 Arrancar y ejecutar (opción 5)

1. Arranque primero la **PC A** (home).
2. Dentro de los 2 minutos siguientes, arranque las demás PCs con la **misma aplicación**.
3. Elija la aplicación en todas las máquinas.

La barrera de cada aplicación espera como máximo 60 s (la demo usa 120 s). Si un nodo arranca tarde, la aplicación termina con el mensaje `fallo la barrera '<etapa>'`.

### 5.5 Consultar estado (opción 6)

Ejecuta en el nodo local: `nodes`, `stats`, `pages` (primeras 15) y `objects`. Si se configuraron IPs, también consulta el directorio global en el home (`./dsmctl --host IP_A --port 27301 dir`).

### 5.6 Ver logs (opción 7)

Muestra las últimas 40 líneas de `logs/dsm-nodeN.log`.

### 5.7 Detener (opción 8)

Envía SIGTERM, espera 2 s, SIGKILL a los que queden, y verifica con `ss -ltn` que el puerto base del rol quedó libre. Si sigue ocupado, muestra PID y proceso y la orden `sudo kill -9 <PID>`.

### 5.8 Diagnóstico (opción 9)

Muestra IPs locales, procesos `dsm_node`, puertos 27300–27315 con PID/proceso, alcance TCP a los peers (puertos 27300–27306) y espacio en disco.

### 5.9 Validar configuración (opción "Validar configuración")

Ejecuta `./dsmctl --config ... config` y muestra la **huella de configuración** (`cluster_hash`). Todas las PCs deben mostrar la misma huella; si difiere, el clúster no arrancará correctamente (error `DSM_ERR_CONFIG_MISMATCH`).

---

## 6. Comandos CLI (`dsmctl`)

```bash
./dsmctl --config config/node0.conf nodes      # tabla de nodos
./dsmctl --config config/node0.conf stats      # estadísticas
./dsmctl --config config/node0.conf pages      # páginas locales
./dsmctl --config config/node0.conf dir        # directorio global (solo home)
./dsmctl --config config/node0.conf objects    # objetos asignados
./dsmctl --config config/node0.conf info       # info del nodo
./dsmctl --config config/node0.conf ping       # pong
./dsmctl --config config/node0.conf config     # huella + parámetros
./dsmctl --config config/node0.conf help       # ayuda
```

Consultar un nodo remoto:

```bash
./dsmctl --host 192.0.2.10 --port 27301 nodes
./dsmctl --host 192.0.2.10 --port 27301 config
```

---

## 7. Panel web

Cada nodo expone un panel HTTP de solo lectura en `web_port` (A: 27303, B: 27307, C: 27311, D: 27315):

```bash
curl -s http://192.0.2.10:27303/            # HTML
curl -s http://192.0.2.10:27303/stats.json  # JSON
```

También se puede abrir en un navegador. El panel muestra estadísticas y contadores del nodo.

---

## 8. Aplicaciones y salidas esperadas

### 8.1 `demo` (2 nodos)

**Comando:** `./dsm.sh` → opción 5 → `demo`.

**Salida esperada (nodo 0):**
```
nodo 0: escribio la pagina 0: 'hola desde el nodo 0'
nodo 0: tras el writeback, pagina 0 -> 'hola desde el nodo 1'
nodo 0: demo: fallos=2 lecturas_remotas=1 escrituras_remotas=1 invalidaciones_enviadas=1 reenvios=1
```

**Nodo 1:** `fallos=2`, 1 lectura y 1 escritura remotas, `errores=0`.

**Qué demuestra:** una página compartida, lectura remota (I→S), escritura remota (S→M con invalidación), writeback (M→S) y reenvío.

### 8.2 `matrices` (2 o 4 nodos)

**Comando:** `./dsm.sh` → opción 5 → `matrices` → tamaño (64 o 192).

**Salida esperada (nodo 0, 64×64, 2 nodos):**
```
nodo 0: matrices 64x64, filas 0..32, 1.72 ms
nodo 0: matrices verificacion: OK (0 discrepancias)
```

**Nodo 1:** `filas 32..64`, `16.82 ms`, `verificacion: OK (0 discrepancias)`.

**Con 4 nodos y 192×192:** 4 bloques de filas; verificación OK en los 4. Latencia mayor por red.

**Qué demuestra:** asignación determinista (mismo `page_base` en todos), coherencia MSI en lectura/escritura, verificación de datos.

### 8.3 `file` (2 nodos)

**Comando:** `./dsm.sh` → opción 5 → `file` → tamaño (1048576 por defecto) o ruta de archivo.

**Salida esperada (nodo 0):**
```
nodo 0: file: patron sintetico de 1048576 bytes
nodo 0: file: checksum completo = 0xc2dc9dc5, 2 bloques, OK
```

**Nodo 1:** `tramo 524288..1048576 checksum 0x21fc9dc5`.

**Con datos reales:** `./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app file --file datos/mi_archivo.txt`

**Qué demuestra:** archivo de 1 MB compartido; cada nodo calcula el checksum de su tramo; el nodo 0 verifica todos los bloques.

### 8.4 `counter` (2 nodos)

**Comando:** `./dsm.sh` → opción 5 → `counter` → iters (100) → threads (2).

**Salida esperada (nodo 0):**
```
nodo 0: counter: suma = 400, esperado = 400, OK
```

**Nodo 1:** `2 hilos x 100 incrementos`.

**Con alta contención:** `--iters 2000 --threads 4` → `suma = 16000, esperado = 16000, OK` (tras la mitigación de una página por nodo).

**Qué demuestra:** coherencia MSI con falsos compartidos; invalidaciones entre nodos.

### 8.5 `bench` (2 nodos)

**Comando:** `./dsm.sh` → opción 5 → `bench`.

**Salida esperada (nodo 0):**
```
nodo 0: bench: lectura local 0.06 us/op, remota 932.72 us/op, escritura remota 680.84 us/op
```

**Métricas derivadas:** throughput 15–31 MB/s lectura remota, 18–34 MB/s escritura remota.

**Qué demuestra:** latencia de fallo de página, transferencia de 4 KB por red, throughput.

### 8.6 `evict` (2 nodos)

**Comando:** `./dsm.sh` → opción 5 → `evict` → pages (64) → frames (32).

**Salida esperada (nodo 0):**
```
nodo 0: evict: expulsiones=100 sucias=46 swap=409600 bytes residentes=32 libres=0 (suma 0)
```

**Nodo 1:** `expulsiones=66 sucias=5 swap=0 bytes residentes=32 libres=0`.

**Qué demuestra:** expulsión LRU, swap en el home, presión de memoria, verificación de datos (suma 0).

### 8.7 `chat` (2, 3 o 4 nodos)

**Comando:** `./dsm.sh` → opción 5 → `chat` → nick (A/B/C/D).

**Uso:**
```
> hola desde A
[nodo B] hola desde A
> /nick A
> /help
> /quit
```

**Qué demuestra:** texto libre entre nodos, coherencia MSI con seqlock, 2/3/4 PCs.

---

## 9. Datos reales (no solo predefinidos)

El sistema soporta datos arbitrarios:

- **`file --file RUTA`**: cualquier archivo (texto, imagen, video). El nodo 0 lo lee y lo comparte; cada nodo calcula el checksum de su tramo; el nodo 0 verifica todos los bloques.
- **`chat`**: cualquier texto que el usuario escriba se propaga a los demás nodos.
- **`matrices --size N`**: cualquier N (probado 64 y 192).
- **`counter --iters N --threads M`**: cualquier N y M (dentro de límites).
- **`evict --pages P --frames F`**: cualquier P y F.

**Ejemplo con archivo real:**

```bash
# En nodo 0
./dsm_node --config config/node0.conf --nodes config/nodes_2.conf \
  --app file --file /ruta/a/mi_archivo.txt --size 1048576

# En nodo 1 (misma línea, sin --file)
./dsm_node --config config/node1.conf --nodes config/nodes_2.conf \
  --app file --size 1048576
```

**Verificación externa:** `sha256sum mi_archivo.txt` para comparar con el checksum interno.

---

## 10. Cambio de configuración en medio del proceso

**Comportamiento:** la configuración se lee una sola vez al arrancar. No hay recarga en caliente.

- Si edita el archivo mientras el nodo corre, el proceso no cambia.
- Si reinicia un nodo con otra IP o puerto, los peers siguen con la tabla vieja; la conexión TCP se corta y el sistema falla rápido (`DSM_ERR_CONFIG_MISMATCH` o `DSM_ERR_PEER_DOWN`), no reconecta a la dirección nueva.
- Para cambiar la configuración: detener todos los nodos, actualizarla en todas las PCs, verificar que la tabla sea idéntica (huella), arrancar A primero y luego B/C/D.

**Procedimiento:**

1. Detener todos los nodos (opción 8 en cada PC, o `kill_leftovers`).
2. En cada PC: `./dsm.sh` → opción 1 → configurar IPs y topología.
3. Verificar la huella en todas las PCs: `./dsmctl --config config/nodeN.conf config`.
4. Arrancar A, luego B/C/D dentro de los 2 minutos.
5. Verificar: `./dsmctl --config config/node0.conf nodes` → debe mostrar N nodos.

**Guion para la demo:** mostrar `dsmctl config` en A y B (misma huella), cambiar `page_size` solo en B, arrancar B → error claro de configuración incompatible, restaurar y reiniciar ordenadamente.

---

## 11. Métricas esperadas (para la demo)

| Métrica | 2 PCs (lab) | 4 nodos (loopback) |
|---|---|---|
| Fallos de página (`demo`) | 2 / 2 | 2 / 2 |
| Latencia lectura local | ~0.06 µs | ~0.06 µs |
| Latencia lectura remota | 680–933 µs | 130–280 µs |
| Latencia escritura remota | 680–860 µs | 120–230 µs |
| Throughput lectura remota | — | 15–31 MB/s |
| Throughput escritura remota | — | 18–34 MB/s |
| `matrices 64×64` | 1.72 ms (A) / 16.82 ms (B) | 18/12/12/12 fallos |
| `matrices 192×192` | — | 162/108/108/108 fallos |
| `counter 100×2` | suma = 400 | suma = 400 |
| `evict 64p/32f` | A: 100 exp, 46 sucias, 409600 B swap | 65/93 fallos, 123/61 exp |
| Errores de coherencia | 0 | 0 |

---

## 12. Solución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| `el puerto 27300 ya está ocupado` | Nodo anterior no se cerró | Opción 8 (detener) u opción 10 (forzar). |
| `fallo la barrera '<etapa>'` | El otro nodo arrancó tarde o no arrancó | Arrancar A primero y B/C/D dentro de 2 minutos. |
| `no se pudo leer la configuración` | Archivo inexistente o mal formado | Usar `./dsm.sh` opción 1 para regenerar. |
| `configuracion invalida` | Parámetros fuera de rango | Revisar `page_size`, `node_memory`, puertos. |
| `DSM_ERR_CONFIG_MISMATCH` | Configuraciones distintas entre PCs | Comparar huellas con `dsmctl config`; unificar. |
| `DSM_ERR_PEER_DOWN` | Nodo caído o firewall | Verificar IP, puertos abiertos, `./dsm.sh` opción 9. |
| `no se pudo abrir <archivo>` (file) | Ruta incorrecta | Verificar la ruta; usar ruta absoluta. |
| Web no responde | `web_enabled=0` o firewall | Verificar `web_enabled=1` y abrir `web_port`. |
| `matrices: FALLO` | Incoherencia de datos | Repetir la prueba; revisar red. |
| Salida con `\\n` literal | Bug de formato en el código | Ya corregido en la versión final. |

---

## 13. Evidencia para la demo

Capturas y logs recomendados:

1. `./dsm.sh` menú y configuración de IPs.
2. `make` exitoso.
3. Arranque de 2/3/4 nodos (`nodo X: listo`).
4. `dsmctl nodes` mostrando N nodos.
5. `dsmctl stats` y `dsmctl pages`.
6. `dsmctl config` (huella) en dos PCs.
7. Salida de `demo`, `matrices`, `file`, `counter`, `bench`, `evict`, `chat`.
8. Panel web (`/` y `/stats.json`).
9. `git log --graph --oneline` con commits de ambos.
10. `git shortlog -sn`.

**Convención de nombres:** `EF-2PC-matrices-A.log`, `EF-4PC-chat-B.log`, `EF-4PC-web-A.png`.

---

## 14. Espacio para el compañero (Parte B — Daniel)

> Esta sección se completa con el trabajo de Daniel Monterrosa.

### 14.1 Lo que le corresponde (según `PLAN_TRABAJO.md`)

- **B1** App `chat` (texto libre).
- **B2** Correcciones de apps (`\n`, counter layout, file con datos reales) y `dsm.sh` multi-nodo (3/4 PCs, roles C/D).
- **B3** Pruebas en 2, 3 y 4 PCs, evidencia, métricas.
- **B4** Informe §4, §5, §14, §15, §16, §18, §20, §21 + manual final.
- **B5** Demo operativa y defensa.

### 14.2 Secciones del manual a completar por Daniel

- [ ] §8.7 `chat`: capturas y guion detallado.
- [ ] §5.4: procedimiento exacto para 3 y 4 PCs (orden, tiempos).
- [ ] §12: soluciones a problemas encontrados en el laboratorio.
- [ ] §13: lista final de capturas con nombres.

### 14.3 Notas del compañero

_(Daniel: agregá aquí tus observaciones, cambios de último momento o decisiones.)_

---

## 15. Resumen rápido para la demo (cheat sheet)

```bash
# En cada PC (una vez)
./dsm.sh   # 1) IPs  2) rol  3) compilar

# Arrancar (A primero, luego B/C/D, < 2 min)
./dsm.sh   # 4) arrancar nodo escuchando   (o 5) con app

# Consultar
./dsmctl --config config/node0.conf nodes
./dsmctl --config config/node0.conf stats
./dsmctl --config config/node0.conf config

# Web
curl -s http://IP:27303/stats.json

# Detener
./dsm.sh   # 8) detener   (o 10) forzar
```

**Orden de la demo:** contexto (5 min) → arranque (10 min) → CLI/stats (15 min) → matrices 4 nodos (20 min) → file/video (20 min) → estrés (15 min) → fallo/tolerancia (10 min) → web/cierre (15 min) → preguntas (10 min).

---

*Fin del manual.*
