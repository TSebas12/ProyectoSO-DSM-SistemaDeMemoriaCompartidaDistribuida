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
| nodo 0 | 0.00 | 83.03 | 31.72 |
| nodo 1 | 0.00 | 59.03 | 54.03 |

Nota: la latencia "local" del nodo que también atiende al peer puede estar contaminada por el servicio a la red; se reporta el valor medido y se documenta la contaminación.

## 16.3 Speedup

Speedup observado con N nodos respecto a 1 nodo (matrices): [MEDIR]. El speedup es sublineal por el overhead de coherencia y transferencia de páginas.

## 16.4 Uso de memoria

| Configuración | Marcos totales | Marcos usados (pico) | % |
|---|---|---|---|
| 2 nodos, matrices 192 | [MEDIR] | [MEDIR] | [MEDIR] |
| 4 nodos, matrices 128 | [MEDIR] | [MEDIR] | [MEDIR] |

## 16.5 Overhead de coherencia

El overhead se mide como el tiempo extra por invalidación y writeback respecto a un acceso local. Se reporta en el anexo 21 junto con los logs de `stats.json`.