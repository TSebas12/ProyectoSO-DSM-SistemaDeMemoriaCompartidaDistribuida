# Guía de Pruebas — DSM (Entrega Final)

> Guía completa para configurar, ejecutar y verificar el sistema DSM en 1, 2, 3 o más PCs. Incluye configuración, apps, casos de fallo y consideraciones para la demo en vivo.

---

## 1. Configuración del sistema

### 1.1 Archivo de configuración (`config/nodeN.conf`)

Cada nodo tiene su propio archivo. Claves principales:

| Clave | Descripción | Ejemplo |
|---|---|---|
| `config_version` | Versión del formato (siempre 1) | `1` |
| `node_id` | ID del nodo (0 = home) | `0` |
| `max_nodes` | Máximo de nodos en el clúster | `16` |
| `bind_address` | IP de escucha (`0.0.0.0` = todas) | `0.0.0.0` |
| `listen_port` | Puerto DSM (TCP) | `27300` |
| `control_port` | Puerto de control (dsmctl) | `27301` |
| `heartbeat_port` | Puerto de heartbeat | `27302` |
| `web_port` | Puerto del panel web | `27303` |
| `web_enabled` | Habilitar panel web (0/1) | `1` |
| `seed_host` | IP del home (nodo 0) | `127.0.0.1` |
| `seed_port` | Puerto DSM del home | `27300` |
| `page_size` | Tamaño de página (potencia de 2, ≥4096) | `4096` |
| `node_memory` | Memoria del nodo en bytes | `1048576` (1 MB = 256 páginas) |
| `window_size` | Ventana de memoria compartida | `268435456` (256 MB) |
| `heartbeat_interval_ms` | Intervalo de heartbeat | `1000` |
| `heartbeat_timeout_ms` | Timeout de heartbeat | `3000` |
| `network_timeout_ms` | Timeout de RPC | `10000` |
| `connect_timeout_ms` | Timeout de conexión | `5000` |
| `retry_count` | Reintentos de RPC | `5` |
| `retry_backoff_ms` | Backoff entre reintentos | `500` |
| `max_message_size` | Tamaño máximo de mensaje | `8192` |
| `thread_pool_size` | Hilos de red | `32` |
| `fault_queue_size` | Cola de fallos de página | `1024` |
| `eviction_policy` | 0=LRU, 1=Clock | `0` |
| `log_level` | 0=off, 1=error, 2=warn, 3=info, 4=debug | `2` |
| `log_file` | Ruta del log | `logs/dsm-node0.log` |
| `stats_interval_ms` | Intervalo de stats | `5000` |
| `barrier_timeout_ms` | Timeout de barrera | `30000` |
| `replication_factor` | Factor de réplica (Extra 2, no implementado) | `0` |
| `port_range_start` | Inicio del rango de puertos | `27300` |
| `port_range_end` | Fin del rango de puertos | `27399` |

### 1.2 Tabla de nodos (`config/nodes_N.conf`)

Formato: `id host puerto` (una línea por nodo).

```
# Tabla de 4 nodos (generada por dsm.sh).
0 127.0.0.1 27300
1 127.0.0.1 27304
2 127.0.0.1 27308
3 127.0.0.1 27312
```

Puertos: `27300 + 4*node_id` (pareja #3, rango 27300–27399).

### 1.3 Configuración vía CLI (`dsm_node`)

| Argumento | Descripción | Ejemplo |
|---|---|---|
| `--config RUTA` | Archivo de config del nodo | `--config config/node0.conf` |
| `--nodes RUTA` | Tabla de nodos | `--nodes config/nodes_4.conf` |
| `--app NOMBRE` | Aplicación a ejecutar | `--app matrices` |
| `--size N` | Tamaño de matriz o bytes de archivo | `--size 128` |
| `--pages N` | Páginas para bench/evict | `--pages 64` |
| `--iters N` | Iteraciones | `--iters 100` |
| `--threads N` | Hilos por nodo (counter) | `--threads 2` |
| `--file RUTA` | Archivo para app file | `--file datos.bin` |
| `--nick NOMBRE` | Nick para chat | `--nick alice` |
| `--text TEXTO` | Texto inicial para chat | `--text "hola"` |
| `--duration S` | Duración en segundos (chat) | `--duration 20` |
| `--frames N` | Marcos por nodo (evict) | `--frames 32` |
| `--no-verify` | Omitir verificación | `--no-verify` |
| `--serve` | Quedar escuchando después de la app | `--serve` |
| `--check-config` | Validar config y salir | `--check-config` |
| `--version` | Versión | `--version` |

### 1.4 Configuración vía `dsmctl`

```bash
./dsmctl --config config/node0.conf stats    # estadísticas
./dsmctl --config config/node0.conf pages    # páginas locales
./dsmctl --config config/node0.conf dir      # directorio (solo home)
./dsmctl --config config/node0.conf nodes    # nodos + heartbeat
./dsmctl --config config/node0.conf config   # huella de configuración
./dsmctl --host IP --port PUERTO stats       # conexión remota
./dsmctl --timeout 10000 ...                # timeout personalizado
```

### 1.5 Panel web

Si `web_enabled=1`:
- `http://HOST:WEB_PORT/` → HTML con estado del nodo.
- `http://HOST:WEB_PORT/stats.json` → JSON con estadísticas.

### 1.6 `dsm.sh` — menú interactivo

| Opción | Función |
|---|---|
| 1 | Configurar topología (2/3/4 nodos) e IPs |
| 2 | Elegir rol de esta máquina (A/B/C/D) |
| 3 | Compilar |
| 4 | Arrancar nodo (escuchando) |
| 5 | Arrancar nodo con app |
| 6 | Consultar stats |
| 7 | Consultar páginas |
| 8 | Detener todo |
| 9 | Diagnóstico de red |
| 10 | Mostrar estado |
| 11 | Validar configuración |

---

## 2. Cómo funciona el sistema (para pruebas)

### 2.1 Arquitectura

- **Home (nodo 0):** único dueño del directorio de páginas. Asigna objetos, sirve lecturas/escrituras, mantiene coherencia.
- **Clientes (nodos 1–N):** solicitan páginas al home. Caché local con estados MSI (M/S/I).
- **Coherencia MSI:** Modified (escritura exclusiva), Shared (lectura compartida), Invalid (no residente).
- **Fallo de página:** SIGSEGV → resolver → fetch remoto → instalar página → reanudar.
- **Heartbeat:** PING/PONG cada 1 s, timeout 3 s. Fail-stop: si un nodo cae, las operaciones que lo necesitan fallan.
- **Barrera:** centralizada en el home, todos los nodos esperan la misma ronda.

### 2.2 Flujo de una operación

1. App accede a memoria compartida (puntero en la ventana).
2. Si la página está en I → SIGSEGV → manejador registra el fallo.
3. Hilo resolver toma el fallo de la cola.
4. Si es lectura: PAGE_REQUEST al home → PAGE_DATA → instalar como S.
5. Si es escritura: OWNERSHIP_REQUEST → home invalida sharers → OWNERSHIP_GRANT → instalar como M.
6. App se reanuda (SIGUSR1).

### 2.3 Estados de página

| Estado | Permiso | Significado |
|---|---|---|
| M (Modified) | RW | Este nodo es el único dueño, puede escribir |
| S (Shared) | R | Copia de lectura, puede haber otros sharers |
| I (Invalid) | Ninguno | No residente, cualquier acceso genera fallo |

---

## 3. Prueba en 1 PC (localhost)

### 3.1 Configuración

```bash
cd Entregable_3/prototipo2
./dsm.sh
# Opción 1 → 4 nodos → IP 127.0.0.1 para todos
# Opción 2 → rol A (esta máquina es todas)
# Opción 3 → compilar
# Opción 11 → validar (misma huella en los 4)
```

### 3.2 Ejecutar demo

```bash
for i in 0 1 2 3; do
  ./dsm_node --config config/node$i.conf --nodes config/nodes_4.conf --app demo &
done
wait
```

**Esperado:** página compartida impresa, 3 barreras completadas, exit 0.

### 3.3 Ejecutar cada app (comandos y salidas esperadas)

| App | Comando | Salida esperada | Exit |
|---|---|---|---|
| matrices | `--app matrices --size 64` | `verificacion: OK (0 discrepancias)` | 0 |
| file | `--app file` | `checksum completo = 0xc2dc9dc5, OK` | 0 |
| file (real) | `--app file --file RUTA` | checksum coincide, OK | 0 |
| file (inexistente) | `--app file --file /tmp/noexiste` | `abortado (archivo no disponible)` | ≠0 |
| counter | `--app counter --iters 100 --threads 2` | `suma = 800, esperado = 800, OK` | 0 |
| bench | `--app bench --iters 500` | latencias en us/op, sin crash | 0 |
| evict | `--app evict --pages 64 --frames 32` | `verificacion OK`, expulsiones>0 | 0 |
| chat | `--app chat --nick alice --duration 10` | mensajes cruzan, exit limpio | 0 |
| task | `--app task --size 1000000` | `speedup=X.XX, balance=Y.YY, OK` | 0 |
| stress | `--app stress` | matrices + counter OK | 0 |

### 3.4 Probar chat interactivamente

```bash
# Terminal 0
./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app chat --nick alice --serve &
# Terminal 1
./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app chat --nick bob
```

Escribir `hola` en terminal 1 → debe aparecer en terminal 0: `[nodo 1] bob: hola`.

Comandos: `/nick`, `/help`, `/quit`. Ctrl+C sale limpio.

### 3.5 Verificar fallo de nodo

```bash
for i in 0 1 2 3; do
  ./dsm_node --config config/node$i.conf --nodes config/nodes_4.conf --app matrices --size 32 &
done
sleep 2
pkill -f "config/node1.conf"
wait
```

**Esperado:** los demás reportan `DSM_ERR_TIMEOUT` o `DSM_ERR_PEER_DOWN`. No hang infinito.

### 3.6 Verificar config mismatch

```bash
cp config/node1.conf /tmp/n1.bak
sed -i 's/^page_size = 4096/page_size = 8192/' config/node1.conf
for i in 0 1; do
  ./dsm_node --config config/node$i.conf --nodes config/nodes_2.conf --app matrices --size 32 &
done
wait
cp /tmp/n1.bak config/node1.conf
```

**Esperado:** error `config mismatch` rápido (no timeout de 10 s).

### 3.7 Diagnóstico

```bash
./dsmctl --config config/node0.conf stats
./dsmctl --config config/node0.conf pages
./dsmctl --config config/node0.conf dir
./dsmctl --config config/node0.conf nodes
./dsmctl --config config/node0.conf config
curl -s http://127.0.0.1:27303/stats.json | python3 -m json.tool | head
```

### 3.8 Detener

```bash
pkill -f dsm_node
rm -f logs/*.log
```

---

## 4. Prueba en 2 PCs

### 4.1 Configuración

**PC A (home):**
```bash
./dsm.sh
# Opción 1 → 2 nodos
# IP_A = IP real de PC A (ej: 192.168.1.10)
# IP_B = IP real de PC B (ej: 192.168.1.20)
# Opción 2 → rol A
# Opción 3 → compilar
# Opción 11 → validar
```

**PC B (cliente):**
```bash
# Copiar el repo (o git clone + pull)
./dsm.sh
# Opción 1 → 2 nodos (mismas IPs)
# Opción 2 → rol B
# Opción 3 → compilar
# Opción 11 → validar (misma huella)
```

### 4.2 Verificar conectividad

```bash
# Desde PC A
ping IP_B
# Desde PC B
ping IP_A
# Verificar puertos abiertos
nc -zv IP_A 27300 27301 27302 27303
```

**Si los puertos están bloqueados:** abrir firewall o usar rango alternativo.

### 4.3 Ejecutar demo en 2 PCs

**PC A:**
```bash
./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app demo --serve
```

**PC B:**
```bash
./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app demo
```

**Esperado:** ambos completan la demo, barreras sincronizadas, exit 0.

### 4.4 Ejecutar apps en 2 PCs

Igual que en 1 PC pero cada nodo en su máquina:
```bash
# PC A
./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app matrices --size 128 --serve &
# PC B
./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app matrices --size 128
```

**Esperado:** `verificacion: OK` en ambos.

### 4.5 Medir latencia real de red

```bash
# PC A y B
./dsm_node --config config/nodeX.conf --nodes config/nodes_2.conf --app bench --iters 1000
```

**Esperado:** latencia remota > latencia local (red real vs loopback).

---

## 5. Prueba en 3 o más PCs

### 5.1 Configuración

Igual que en 2 PCs pero con 3 o 4 nodos:
- `dsm.sh` opción 1 → `3` o `4`.
- Asignar rol A/B/C/D a cada máquina.
- Ingresar la IP real de cada PC.
- Validar huella en todas.

### 5.2 Ejecutar con 4 PCs

```bash
# PC A (home)
./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 256 --serve &
# PC B
./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 256 &
# PC C
./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 256 &
# PC D
./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size 256
```

**Esperado:** speedup real con 4 nodos, verificación OK en todos.

### 5.3 Chat entre 4 PCs

```bash
# Cada PC ejecuta chat con su nick
./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app chat --nick nodoN
```

Escribir en cualquier PC → debe aparecer en los demás.

---

## 6. Consideraciones para la demo en vivo

### 6.1 Preparación

- [ ] Repositorio clonado y compilado en cada PC.
- [ ] Configuración de respaldo (USB o copia local).
- [ ] Puertos del laboratorio probados (27300–27399).
- [ ] Datos de prueba listos (`/tmp/prueba.txt`).
- [ ] Logs o ejecución grabada de respaldo.
- [ ] Ensayo de la explicación de cada módulo (ambos integrantes).

### 6.2 Guion sugerido (120 min)

1. **0–5 min:** contexto, arquitectura, objetivos.
2. **5–15 min:** arranque del clúster y configuración.
3. **15–30 min:** CLI: nodos, heartbeat, estadísticas, tablas de páginas.
4. **30–50 min:** matrices con 4 nodos.
5. **50–70 min:** file/video y verificación de checksum.
6. **70–85 min:** estrés y coherencia.
7. **85–95 min:** fallo de nodo (kill) y fail-stop.
8. **95–105 min:** chat en vivo.
9. **105–115 min:** panel web y cierre ordenado.
10. **115–120 min:** preguntas y defensa.

### 6.3 Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Red del laboratorio falla | Config de respaldo + ejecución grabada |
| Puertos ocupados | Rango alternativo (27300–27399) + validación previa |
| Un nodo no arranca | Scripts de arranque + logs en `logs/` |
| App tarda demasiado | Dataset ajustado (size 64, iters 100) |
| Un integrante no puede explicar | Ensayo cruzado de todas las partes |
| Extras fallan | Desactivarlos por config y documentar |
| Git no muestra autoría | Verificar commits antes de la demo |

### 6.4 Comandos de emergencia

```bash
# Matar todo
pkill -f dsm_node
# Limpiar logs
rm -f logs/*.log
# Recompilar
make clean && make
# Validar config
./dsm.sh  # opción 11
# Verificar huella
for i in 0 1 2 3; do ./dsmctl --config config/node$i.conf config | grep huella; done
```

---

## 7. Configuración avanzada (para ajustar rendimiento)

### 7.1 Memoria y páginas

- `node_memory = 1048576` (1 MB = 256 páginas de 4 KB). Reducir para forzar más expulsiones.
- `page_size = 4096` (mínimo). Aumentar a 8192 para menos páginas pero más datos por página.
- `window_size = 268435456` (256 MB). Determina cuántas páginas globales caben.

### 7.2 Red y timeouts

- `network_timeout_ms = 10000`: timeout de RPC. Reducir para detectar fallos más rápido.
- `connect_timeout_ms = 5000`: timeout de conexión inicial.
- `retry_count = 5`: reintentos antes de declarar fallo.
- `heartbeat_interval_ms = 1000`, `heartbeat_timeout_ms = 3000`: detección de nodo caído en ~3 s.

### 7.3 Coherencia y concurrencia

- `thread_pool_size = 32`: hilos de red. Reducir en máquinas con pocos cores.
- `fault_queue_size = 1024`: cola de fallos de página. Aumentar si hay muchos hilos.
- `eviction_policy = 0` (LRU) o `1` (Clock).
- `replication_factor = 0` (Extra 2 no implementado).

### 7.4 Logging y debug

- `log_level = 4` (debug) para ver todo; `2` (warn) para producción.
- `log_file = logs/dsm-nodeN.log`: un archivo por nodo.
- `stats_interval_ms = 5000`: intervalo de reporte de stats.

---

## 8. Verificación de coherencia MSI (manual)

### 8.1 Estados de página

```bash
./dsmctl --config config/node0.conf dir
```

**Salida esperada:** tabla con `page_id`, `owner_node`, `state` (M/S/I), `sharers`, `version`.

### 8.2 Invalidaciones

```bash
./dsmctl --config config/node0.conf stats | grep -i invalid
```

**Esperado:** `invalidations_sent > 0` y `invalidations_received > 0` después de escrituras compartidas.

### 8.3 Swap

```bash
./dsmctl --config config/node0.conf stats | grep -i swap
```

**Esperado:** `swap_bytes > 0` después de `evict`.

---

## 9. Checklist final

- [ ] Código compila sin warnings (`-Wall -Wextra -Wpedantic`).
- [ ] `dsm_node` y `dsmctl` generados.
- [ ] Configuración validada (misma huella en todos los nodos).
- [ ] Demo ejecuta sin errores.
- [ ] Matrices: `verificacion: OK`.
- [ ] File: `checksum completo = 0xc2dc9dc5, OK`.
- [ ] Counter: `suma = esperado, OK`.
- [ ] Bench: latencias impresas.
- [ ] Evict: `verificacion OK`, expulsiones > 0.
- [ ] Chat: mensajes cruzan.
- [ ] Task: `speedup > 1, balance ~1, OK`.
- [ ] Stress: OK.
- [ ] Fail-stop: nodo caído detectado.
- [ ] Config mismatch: detectado sin timeout.
- [ ] Ctrl+C: salida limpia.
- [ ] Panel web: responde.
- [ ] Informe: 21 secciones.
- [ ] Manual: completo.
- [ ] Git: commits de ambos integrantes.
- [ ] Sin datos sensibles en el árbol actual.

---

*Fin de la guía.* Con esto tenés todo para configurar, probar y verificar el sistema en 1, 2, 3 o más PCs, incluyendo la demo en vivo. Si algo falla, copiá el comando exacto, la salida y el log del nodo afectado.