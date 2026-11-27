# 16. Rendimiento

## 16.1 Métricas

| Métrica | Descripción | Unidad |
|---|---|---|
| Latencia de acceso local | Tiempo por acceso a página residente | us/op |
| Latencia de acceso remoto | Tiempo por acceso a página no residente (fallo de página + transferencia) | us/op |
| Latencia de escritura | Tiempo por escritura que dispara invalidación | us/op |
| Throughput | Páginas transferidas por segundo | páginas/s |
| Uso de memoria | Marcos residentes / marcos totales | % |
| Expulsiones | Páginas expulsadas a swap | count |

## 16.2 Resultados de bench

| Nodo | Local (us/op) | Remoto (us/op) | Escritura (us/op) |
|---|---|---|---|
| nodo 0 | 0.03 | 64.31 | 42.09 |
| nodo 1 | 0.00 | 64.00 | 60.59 |

Nota: la latencia "local" del nodo que también atiende al peer puede estar contaminada por el servicio a la red; se reporta el valor medido y se documenta la contaminación.

## 16.3 Speedup

Speedup observado con N nodos respecto a 1 nodo (matrices 128×128):

| Nodos | Tiempo (ms) | Speedup |
|---|---|---|
| 1 (secuencial) | 0.11* | 1.00x |
| 2 | 4.67 (máx) | 0.02x |
| 4 | 3.75 (máx) | 0.03x |

* Baseline 1 nodo (matrices 64×64): 0.11 ms. Para matrices 128×128 el baseline se estima en 0.44 ms (escalado cuadrático). El speedup es sublineal por el overhead de coherencia (invalidaciones, transferencias de página, barreras) y la contención de red en loopback.

Mediciones reales:
- 1 nodo (64×64): 0.11 ms
- 2 nodos (128×128): nodo 0 = 1.55 ms, nodo 1 = 4.67 ms (máx)
- 4 nodos (128×128): nodo 0 = 0.63 ms, nodo 1 = 3.52 ms, nodo 2 = 3.50 ms, nodo 3 = 3.75 ms (máx)

## 16.4 Uso de memoria

| Configuración | Marcos totales | Marcos usados (pico aprox.) | % uso |
|---|---|---|---|
| 2 nodos, matrices 192 | 512 | 360 | 70.3% |
| 4 nodos, matrices 128 | 1024 | 288 | 28.1% |
| 2 nodos, matrices 128 | 512 | 160 | 31.3% |
| 4 nodos, matrices 64 | 1024 | 72 | 7.0% |

* Marcos totales = nodos × 256 (1 MB / 4 KB por nodo).
* Marcos usados (pico aprox.) = páginas residentes finales observadas × nodos. El pico real durante la ejecución puede ser ligeramente superior; no hay monitoreo continuo de `dsm_memory_usage` durante la ejecución.
* % uso = (usados / totales) × 100.

Detalle por configuración:
- 2 nodos, matrices 192: 180 páginas/nodo × 2 = 360 marcos (70.3%)
- 4 nodos, matrices 128: 72 páginas/nodo × 4 = 288 marcos (28.1%)
- 2 nodos, matrices 128: 80 páginas/nodo × 2 = 160 marcos (31.3%)
- 4 nodos, matrices 64: 18 páginas/nodo × 4 = 72 marcos (7.0%)

## 16.5 Overhead de coherencia

El overhead se mide como el tiempo extra por invalidación y writeback respecto a un acceso local. Se reporta en el anexo 21 junto con los logs de `stats.json`.

Métricas observadas (bench 2 nodos):
- Latencia RPC media: 40-41 us
- Throughput lectura remota: ~60-61 MB/s
- Throughput escritura remota: ~64-93 MB/s
- Reintentos RPC: ~36 en 90s (tasa ~0.4/s)