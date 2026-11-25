# Guía de Pruebas en Laboratorio — DSM (4 PCs)

> Documento para el equipo. Incluye paso a paso para todas las pruebas posibles en el laboratorio con 4 PCs, qué capturar en pantalla para documentar cada prueba, y qué resultado esperar. Usar junto a `GUIA_PRUEBAS.md`.

---

## 0. Preparación previa (antes de ir al lab)

### 0.1 En cada PC

1. Clonar el repo y hacer pull:
   ```bash
   git clone https://github.com/TSebas12/ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida.git
   cd ProyectoSO-DSM-SistemaDeMemoriaCompartidaDistribuida
   git pull --ff-only
   ```
2. Compilar:
   ```bash
   cd Entregable_3/prototipo2
   make clean && make
   ls -l dsm_node dsmctl
   ```
3. Crear directorio de logs:
   ```bash
   mkdir -p logs
   ```

### 0.2 Configurar topología (en PC A = home)

```bash
./dsm.sh
# Opción 1 → 4 nodos
# IP_A = IP real de PC A
# IP_B = IP real de PC B
# IP_C = IP real de PC C
# IP_D = IP real de PC D
# Opción 2 → rol A
# Opción 11 → validar
```

### 0.3 Copiar configuración a las otras PCs

Las PCs B, C, D necesitan los mismos archivos de config. Opciones:
- Copiar `config/` por USB o scp:
  ```bash
  scp -r config/ usuario@IP_B:~/ProyectoSO/Entregable_3/prototipo2/
  scp -r config/ usuario@IP_C:~/ProyectoSO/Entregable_3/prototipo2/
  scp -r config/ usuario@IP_D:~/ProyectoSO/Entregable_3/prototipo2/
  ```
- O ejecutar `./dsm.sh` en cada PC con las mismas IPs y rol correspondiente.

### 0.4 Configurar rol en cada PC

| PC | Rol | Comando |
|---|---|---|
| PC A | A (home) | `./dsm.sh` → opción 2 → A |
| PC B | B | `./dsm.sh` → opción 2 → B |
| PC C | C | `./dsm.sh` → opción 2 → C |
| PC D | D | `./dsm.sh` → opción 2 → D |

### 0.5 Validar en todas las PCs

En cada PC:
```bash
./dsm.sh  # opción 11
```

**Captura 0:** Pantalla de validación mostrando `OK` y la misma huella en las 4 PCs.

### 0.6 Verificar conectividad

```bash
# Desde PC A
ping -c 3 IP_B; ping -c 3 IP_C; ping -c 3 IP_D
nc -zv IP_B 27304; nc -zv IP_C 27308; nc -zv IP_D 27312
```

**Captura 1:** Resultado de ping y nc exitoso entre PCs.

---

## 1. Prueba 1 — Demo (humo)

### Pasos

1. En PC A: `./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app demo --serve`
2. En PC B: `./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app demo`
3. En PC C: `./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app demo`
4. En PC D: `./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app demo`

### Qué debe salir

- Cada PC imprime la página compartida.
- Barreras: 3 rondas completadas en todos.
- Sin `FALLO`, `timeout`, `SIGSEGV`.

### Capturas

- **Captura 2:** Pantalla de PC A mostrando la demo completa con barreras.
- **Captura 3:** Pantalla de PC B/C/D mostrando la misma página compartida.

---

## 2. Prueba 2 — Matrices

### Pasos

1. En PC A: `./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app matrices --size 128 --serve &`
2. En PC B: `./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app matrices --size 128 &`
3. En PC C: `./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app matrices --size 128 &`
4. En PC D: `./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app matrices --size 128`
5. Esperar a que todos terminen.

### Qué debe salir

- Cada PC: `verificacion: OK (0 discrepancias)`.
- Exit code 0 en todos.

### Capturas

- **Captura 4:** Pantalla de cualquier PC mostrando `verificacion: OK (0 discrepancias)`.
- **Captura 5:** Pantalla de otra PC mostrando lo mismo (para evidenciar que todos verificaron).

---

## 3. Prueba 3 — File (checksum distribuido)

### Pasos

1. Crear archivo de prueba en PC A:
   ```bash
   echo "datos de prueba DSM para checksum distribuido" > /tmp/prueba.txt
   ```
2. Copiar a las demás PCs (o crear el mismo archivo):
   ```bash
   scp /tmp/prueba.txt usuario@IP_B:/tmp/prueba.txt
   scp /tmp/prueba.txt usuario@IP_C:/tmp/prueba.txt
   scp /tmp/prueba.txt usuario@IP_D:/tmp/prueba.txt
   ```
3. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app file --file /tmp/prueba.txt
   ```

### Qué debe salir

- Nodo 0: `checksum completo = 0x..., OK`.
- Exit 0 en todos.

### Capturas

- **Captura 6:** Nodo 0 mostrando checksum OK.
- **Captura 7:** Nodo 1/2/3 mostrando su tramo y checksum parcial.

### Prueba adicional — archivo inexistente

```bash
./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app file --file /tmp/noexiste.bin
```

**Esperado:** `abortado (archivo no disponible)`, exit ≠ 0.

---

## 4. Prueba 4 — Counter

### Pasos

1. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app counter --iters 100 --threads 2
   ```

### Qué debe salir

- Nodo 0: `suma = 800, esperado = 800, OK` (4 × 2 × 100 = 800).

### Capturas

- **Captura 8:** Nodo 0 mostrando `suma = 800, esperado = 800, OK`.

---

## 5. Prueba 5 — Bench (latencia de red real)

### Pasos

1. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app bench --iters 1000
   ```

### Qué debe salir

- Latencias en us/op por nodo: lectura local, lectura remota, escritura.
- Latencia remota > latencia local (red real vs loopback).

### Capturas

- **Captura 9:** Comparación de latencias local vs remota en al menos 2 PCs.

---

## 6. Prueba 6 — Evict (swap y expulsión)

### Pasos

1. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app evict --pages 64 --frames 32
   ```

### Qué debe salir

- `verificacion OK` en cada nodo.
- `expulsiones > 0`, `swap > 0 bytes` (en el home/PC A).
- `residentes` y `libres` coherentes.

### Capturas

- **Captura 10:** PC A (home) mostrando stats con swap_bytes > 0.
- **Captura 11:** Cualquier PC mostrando `verificacion OK`.

---

## 7. Prueba 7 — Chat (tiempo real)

### Pasos

1. En PC A: `./dsm_node --config config/node0.conf --nodes config/nodes_4.conf --app chat --nick alice --serve`
2. En PC B: `./dsm_node --config config/node1.conf --nodes config/nodes_4.conf --app chat --nick bob`
3. En PC C: `./dsm_node --config config/node2.conf --nodes config/nodes_4.conf --app chat --nick carlos`
4. En PC D: `./dsm_node --config config/node3.conf --nodes config/nodes_4.conf --app chat --nick diana`

Escribir un mensaje en PC B → debe aparecer en A, C, D.

### Qué debe salir

- Mensajes cruzan entre PCs en tiempo real.
- `/nick`, `/help`, `/quit` funcionan.
- Ctrl+C sale limpio.

### Capturas

- **Captura 12:** PC B escribiendo un mensaje.
- **Captura 13:** PC A/C/D mostrando el mensaje recibido.

---

## 8. Prueba 8 — Task (Extra 1: ejecución distribuida)

### Pasos

1. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app task --size 1000000
   ```

### Qué debe salir

- Cada nodo: su porción completada con tiempo.
- Nodo 0 (coordinador): `total paralelo = X, total secuencial = Y, OK`.
- `speedup > 1.0`, `balance` cercano a 1.0.

### Capturas

- **Captura 14:** Nodo 0 mostrando speedup y balance.
- **Captura 15:** Un trabajador mostrando su porción y tiempo.

---

## 9. Prueba 9 — Stress

### Pasos

1. Ejecutar en las 4 PCs:
   ```bash
   ./dsm_node --config config/nodeN.conf --nodes config/nodes_4.conf --app stress
   ```

### Qué debe salir

- Matrices 192 OK + counter OK en todos.
- Sin crash ni timeout.

### Capturas

- **Captura 16:** Cualquier PC mostrando matrices OK + counter OK.

---

## 10. Prueba 10 — Fallo de nodo (fail-stop)

### Pasos

1. Arrancar clúster con matrices en las 4 PCs.
2. En PC B: `Ctrl+C` o `kill -9 <PID>` a los 2 segundos.
3. Observar las demás PCs.

### Qué debe salir

- Las demás reportan `DSM_ERR_TIMEOUT` o `DSM_ERR_PEER_DOWN`.
- No hang infinito (barrera tiene timeout de 60 s).
- Exit ≠ 0 en los afectados.

### Capturas

- **Captura 17:** PC A mostrando error de timeout/peer down.
- **Captura 18:** PC C/D mostrando el mismo error.

---

## 11. Prueba 11 — Config mismatch

### Pasos

1. En PC B, cambiar `page_size` en `config/node1.conf` a `8192`.
2. Arrancar matrices en PC A y B.
3. Observar.

### Qué debe salir

- Error `config mismatch` rápido (no espera 10 s).
- Restaurar config después.

### Capturas

- **Captura 19:** Error de config mismatch en la terminal.

---

## 12. Prueba 12 — Diagnóstico y CLI

### Pasos

En PC A (home):
```bash
./dsmctl --config config/node0.conf stats
./dsmctl --config config/node0.conf pages
./dsmctl --config config/node0.conf dir
./dsmctl --config config/node0.conf nodes
./dsmctl --config config/node0.conf config
```

En PC B (remoto):
```bash
./dsmctl --host IP_A --port 27301 stats
```

### Qué debe salir

- Stats: contadores de páginas, fallos, invalidaciones, etc.
- Pages: tabla de páginas locales con estado.
- Dir: directorio completo (solo en home).
- Nodes: 4 nodos con `alive=1` y `last_seen` reciente.
- Config: huella idéntica a las demás PCs.

### Capturas

- **Captura 20:** `dsmctl stats` en PC A.
- **Captura 21:** `dsmctl nodes` mostrando los 4 nodos vivos.
- **Captura 22:** `dsmctl dir` en PC A.
- **Captura 23:** `dsmctl` remoto desde PC B a PC A.

---

## 13. Prueba 13 — Panel web

### Pasos

1. En PC A, abrir navegador: `http://IP_A:27303/`
2. Abrir `http://IP_A:27303/stats.json`

### Qué debe salir

- Página HTML con estado del nodo.
- JSON con estadísticas.

### Capturas

- **Captura 24:** Panel web en navegador.
- **Captura 25:** stats.json en navegador o curl.

---

## 14. Prueba 14 — Verificación de huella (A2)

### Pasos

En cada PC:
```bash
./dsmctl --config config/nodeN.conf config | grep huella
```

### Qué debe salir

- La misma huella en las 4 PCs.

### Capturas

- **Captura 26:** Las 4 huellas idénticas (puede ser en una sola foto o captura por PC).

---

## 15. Prueba 15 — Verificación de Git

### Pasos

```bash
git shortlog -sn
git log --oneline -5
```

### Qué debe salir

- Commits de Sebastian Aguero y Monterrosa.
- Historial con fechas distribuidas.

### Capturas

- **Captura 27:** `git shortlog` mostrando ambos autores.

---

## 16. Resumen de capturas para documentar

| # | Qué capturar | Dónde |
|---|---|---|
| 0 | Validación de config (OK + huella) | Todas las PCs |
| 1 | Ping/nc entre PCs | PC A |
| 2 | Demo completa con barreras | PC A |
| 3 | Demo en PC B/C/D (misma página) | PC B/C/D |
| 4 | Matrices OK | Cualquier PC |
| 5 | Matrices OK (otra PC) | Otra PC |
| 6 | File checksum OK (nodo 0) | PC A |
| 7 | File tramo/checksum parcial | PC B/C/D |
| 8 | Counter OK | PC A |
| 9 | Bench latencias local vs remota | 2 PCs |
| 10 | Evict stats con swap | PC A |
| 11 | Evict verificacion OK | Cualquier PC |
| 12 | Chat: mensaje escrito | PC B |
| 13 | Chat: mensaje recibido | PC A/C/D |
| 14 | Task: speedup/balance (coord) | PC A |
| 15 | Task: porción completada | PC B/C/D |
| 16 | Stress OK | Cualquier PC |
| 17 | Fail-stop: error en PC A | PC A |
| 18 | Fail-stop: error en PC C/D | PC C/D |
| 19 | Config mismatch | Terminal afectada |
| 20 | dsmctl stats | PC A |
| 21 | dsmctl nodes (4 vivos) | PC A |
| 22 | dsmctl dir | PC A |
| 23 | dsmctl remoto desde PC B | PC B |
| 24 | Panel web | Navegador |
| 25 | stats.json | Navegador/curl |
| 26 | Huellas idénticas (4 PCs) | Todas |
| 27 | git shortlog (autores) | Cualquier PC |

---

## 17. Limpieza final

```bash
# En cada PC
pkill -f dsm_node
rm -f logs/*.log
# Verificar que no queden procesos
pgrep -f dsm_node && echo "WARNING" || echo "OK: limpio"
```

---

*Fin de la guía de laboratorio. Con estas 27 capturas se documentan todas las pruebas posibles en 4 PCs.*