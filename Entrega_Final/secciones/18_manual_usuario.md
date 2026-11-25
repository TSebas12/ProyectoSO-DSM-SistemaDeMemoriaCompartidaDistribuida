# 18. Manual de usuario

## 18.1 Requisitos

- Linux (x86_64), `gcc`, `make`, `bash`.
- Red entre nodos (loopback para pruebas locales; 2–4 PCs para el laboratorio).
- Repositorio clonado y actualizado a `origin/main`.

Verificación:

```bash
gcc --version
make --version
```

## 18.2 Compilación

```bash
cd Entregable_3/prototipo2
make clean && make
ls -l dsm_node dsmctl
```

## 18.3 Configuración

Usar `./dsm.sh`:

1. Opción 1 — Configurar nodos: elegir 2, 3 o 4 nodos e ingresar las IPs.
2. Opción 2 — Elegir rol de esta máquina (A/B/C/D).
3. Opción 11 — Validar configuración: debe imprimir `OK` para cada nodo y la misma huella en todos.

Alternativa manual: editar `config/nodeN.conf` y `config/nodes_N.conf` (no recomendado).

## 18.4 Arranque

- Opción 4 — Arrancar el nodo escuchando.
- Opción 5 — Arrancar el nodo con una aplicación.

Manualmente:

```bash
# Nodo 0 (home)
./dsm_node --config config/node0.conf --nodes config/nodes_2.conf --app demo --serve &
# Nodo 1
./dsm_node --config config/node1.conf --nodes config/nodes_2.conf --app demo
```

## 18.5 Comandos CLI (`dsmctl`)

```bash
./dsmctl --config config/node0.conf stats    # estadísticas
./dsmctl --config config/node0.conf pages    # páginas locales
./dsmctl --config config/node0.conf dir      # directorio (solo home)
./dsmctl --config config/node0.conf nodes    # nodos + heartbeat
./dsmctl --config config/node0.conf config   # huella de configuración
```

## 18.6 Panel web

Si `web_enabled=1`: `http://127.0.0.1:27303/` y `http://127.0.0.1:27303/stats.json`.

## 18.7 Aplicaciones

| App | Comando típico |
|---|---|
| demo | `--app demo` |
| matrices | `--app matrices --size 128` |
| file | `--app file` / `--app file --file RUTA` |
| counter | `--app counter --iters 100 --threads 2` |
| bench | `--app bench --iters 2000` |
| evict | `--app evict --pages 64 --frames 32` |
| chat | `--app chat --nick alice --duration 20` |
| stress | `--app stress` |

## 18.8 Solución de problemas

- **bind: Address already in use:** nodo anterior no terminó. `pkill -f dsm_node`, esperar 2 s, reintentar.
- **config mismatch:** comparar `dsmctl config` en cada nodo; revisar `page_size`, `node_memory`, `max_message_size`, `port_range_*`, tabla de nodos.
- **no se pudo conectar:** el home (nodo 0) no está corriendo o `seed_host`/`seed_port` mal.
- **Timeouts en matrices/file:** arrancar los nodos con menos de 2 min de diferencia (barrera inicial espera 60 s).
- **Chat no muestra nada:** verificar que ambos nodos hayan pasado la barrera inicial y que la duración no haya expirado; revisar `logs/dsm-nodeN.log`.
- **Logs:** `tail -f logs/dsm-node0.log`.

## 18.9 Detener todo

```bash
./dsm.sh   # opción 8 (stop)
# o
pkill -f dsm_node
rm -f logs/*.log
```