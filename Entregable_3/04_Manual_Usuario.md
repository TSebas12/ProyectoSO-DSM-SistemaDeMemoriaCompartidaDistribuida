# Manual de usuario — Prototipo 2 del DSM

**Proyecto:** Sistema de Memoria Compartida Distribuida (DSM) con paginación bajo demanda y coherencia MSI
**Curso:** IC-6600 Principios de Sistemas Operativos
**Entregable:** 3 — Prototipo 2
**Interfaz principal:** `./dsm.sh` (menú interactivo). Alternativa para WSL/loopback: `lab.sh`.

---

## 1. Requisitos

- **Sistema operativo:** Linux (probado en Linux Mint 22.3 y WSL2; Ubuntu 24.04 y Debian 12 también funcionan). macOS no está soportado.
- **Compilador:** `gcc` con soporte C11 (`gcc --version`).
- **Librerías:** `pthreads`, `librt`, `libm` (incluidas en glibc; en Alpine instalar `build-base`).
- **Red:** dos PCs en la misma red para la prueba de laboratorio; una sola máquina basta para WSL/loopback.
- **Herramientas opcionales:** `ss` (iproute2) y `nc` para diagnóstico; `make`.

Verificación rápida:

```bash
gcc --version
make --version
ss -V
```

---

## 2. Obtener y preparar el proyecto

```bash
git clone https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git
cd ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida/Entregable_3/prototipo2
chmod +x dsm.sh lab.sh build.sh
```

No edite los archivos `config/*.conf` a mano: `./dsm.sh` (opción 1) los actualiza.

---

## 3. Menú principal (`./dsm.sh`)

Al ejecutar `./dsm.sh` aparece un menú con 10 opciones:

| Opción | Acción |
|---|---|
| 1 | Configurar IPs de las dos máquinas (A y B). |
| 2 | Elegir rol de esta máquina (A/B/C/D). |
| 3 | Compilar `dsm_node` y `dsmctl`. |
| 4 | Arrancar el nodo escuchando. |
| 5 | Arrancar el nodo con una aplicación. |
| 6 | Consultar estado (nodos, stats, páginas, objetos). |
| 7 | Ver el log de este nodo. |
| 8 | Detener los nodos (SIGTERM → SIGKILL, verifica el puerto). |
| 9 | Diagnóstico de red (IPs, procesos, puertos con PID/proceso, alcance al peer). |
| 10 | Forzar liberación de puertos (SIGKILL directo). |
| 0 | Salir. |

El estado (rol e IPs) se guarda en `.dsm_state` (ignorado por Git) y se carga al volver a abrir el menú.

---

## 4. Flujo de trabajo en el laboratorio (2 PCs)

### 4.1 Configurar IPs (opción 1)

En **las dos PCs**, con los mismos valores:

```
IP de la máquina A (home, nodo 0): 192.0.2.10
IP de la máquina B (cliente, nodo 1): 192.0.2.11
```

Ejemplo con direcciones de documentación (RFC 5737): A `192.0.2.10`, B `192.0.2.11`. En el laboratorio se usan las IP asignadas a cada PC.

Esto actualiza `config/nodes.conf`, `config/nodes_2.conf` (nodos 0 y 1) y el `seed_host` de los nodos 1–3.

### 4.2 Elegir rol (opción 2)

- PC A: rol **A** (home / nodo 0), config `config/node0.conf`, puerto base 27300.
- PC B: rol **B** (cliente / nodo 1), config `config/node1.conf`, puerto base 27304.

Los roles C y D corresponden a los nodos 2 y 3 (puertos 27308 y 27312) para despliegues de 4 PCs.

### 4.3 Compilar (opción 3)

Genera `dsm_node` y `dsmctl`. Si falla, revise que `gcc` y las cabeceras estén instaladas.

### 4.4 Arrancar y ejecutar (opción 5)

1. Arranque primero la **PC A** (home).
2. Dentro de los 2 minutos siguientes, arranque la **PC B** con la misma aplicación.
3. Elija la aplicación en ambas máquinas:

| Opción del menú de apps | Aplicación | Qué imprime |
|---|---|---|
| 1 | `demo` | Nodo 0: `tras el writeback, pagina 0 -> 'hola desde el nodo 1'`; 2 fallos de página, 3 barreras, 0 errores. |
| 2 | `file` (1 MB) | `checksum completo = 0xc2dc9dc5, 2 bloques, OK` (nodo 0); cada nodo imprime su tramo y checksum parcial. |
| 3 | `counter` | `suma = 400, esperado = 400, OK` (nodo 0) con `--iters 100 --threads 2`. |
| 4 | `bench` | Latencias por fase: lectura local, lectura remota, escritura remota (µs/op y MB/s). |
| 5 | `evict` (64 páginas / 32 marcos) | Expulsiones, sucias, bytes de swap, residentes y libres. |
| 6 | `matrices` | `verificacion: OK (0 discrepancias)` por nodo; tiempo por bloque de filas. |

La barrera de cada aplicación espera como máximo 60 s. Si un nodo arranca tarde, la aplicación termina con el mensaje `fallo la barrera '<etapa>'`.

### 4.5 Consultar estado (opción 6)

Ejecuta en el nodo local: `nodes`, `stats`, `pages` (primeras 15) y `objects`. Si se configuraron IPs, también consulta el directorio global en el home.

### 4.6 Ver logs (opción 7)

Muestra las últimas 40 líneas de `logs/dsm-nodeN.log`.

### 4.7 Detener (opción 8)

Envía SIGTERM, espera 2 s, SIGKILL a los que queden, y verifica con `ss -ltn` que el puerto base del rol quedó libre. Si sigue ocupado, muestra PID y proceso y la orden `sudo kill -9 <PID>`.

### 4.8 Diagnóstico (opción 9)

Muestra IPs locales, procesos `dsm_node`, puertos 27300–27315 con PID/proceso, alcance TCP a la otra máquina (puertos 27300–27306) y espacio en disco.

### 4.9 Forzar liberación (opción 10)

SIGKILL directo sobre los procesos `dsm_node` y `fuser -k` sobre los puertos 27300–27315. Usar solo si la opción 8 no liberó el puerto.

---

## 5. Firewall

Abrir TCP `27300–27399` (o al menos `27300–27315`) en cada PC:

```bash
# Ejemplo con ufw
sudo ufw allow 27300:27399/tcp
```

---

## 6. Problemas comunes

| Síntoma | Causa probable | Solución |
|---|---|---|
| `el puerto 27300 ya está ocupado` | Nodo anterior no terminó o puerto tomado por otro programa. | Opción 8; si persiste, opción 10 o `sudo kill -9 <PID>` (PID mostrado por la opción 9). |
| `fallo la barrera '<etapa>'` | Un nodo arrancó con más de 2 minutos de diferencia o no arrancó. | Arrancar la PC A primero y la B dentro de los 2 minutos. |
| `checksum ... FALLO` | Incoherencia de páginas o datos no propagados. | Repetir la corrida; verificar que ambas PCs usen la misma tabla `nodes_2.conf`. |
| `sin respuesta` en el diagnóstico | Firewall o IP incorrecta. | Revisar firewall y la opción 1 (IPs iguales en ambas PCs). |
| Error de compilación | Falta `gcc` o cabeceras. | Instalar `build-essential` (Debian/Ubuntu/Mint) o `build-base` (Alpine). |
| `counter` con `--iters 2000 --threads 4` da menos del esperado | Deuda técnica documentada (cesión de propiedad del home). | Usar `--iters 100 --threads 2` para resultados exactos. |

---

## 7. Alternativa para WSL / loopback (lab.sh)

Para pruebas locales sin menú:

```bash
make
./lab.sh demo
./lab.sh matrices --size 64
./lab.sh file
./lab.sh counter --iters 100 --threads 2
./lab.sh bench
./lab.sh evict
./lab.sh stress
./lab.sh serve 0
./lab.sh ctl 0 stats
./lab.sh stop
```

`lab.sh` usa `nodes_2.conf` (2 nodos) para la mayoría de apps y `nodes.conf` (4 nodos) para `matrices` y `serve`.

---

## 8. Cómo detener todo

- Menú: opción **8** (verifica el puerto).
- Si queda algo colgado: opción **10** (SIGKILL).
- Manual: `pkill -f dsm_node` y verificar con `ss -ltn | grep 273`.

---

## 9. Panel web (opcional)

Si `web_enabled = 1` en el config (por defecto en `node0.conf`), el home expone:

- `http://<host>:27303/` — HTML de solo lectura.
- `http://<host>:27303/stats.json` — JSON con nodos, stats, páginas y objetos.

Requiere un navegador o cliente HTTP en la red del laboratorio.