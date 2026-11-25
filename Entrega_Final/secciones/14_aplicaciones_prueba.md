# 14. Aplicaciones de prueba

Todas las aplicaciones usan el espacio DSM mediante `dsm_malloc` y se sincronizan con la barrera centralizada. Se ejecutan con `--app <nombre>`.

## 14.1 Matrices (`matrices`)

Multiplicación de matrices por bloques sobre el espacio compartido. Cada nodo calcula un bloque de la matriz resultado `C = A × B`. Al finalizar, todos los nodos verifican que `C` coincida con la referencia calculada localmente. Imprime `verificacion: OK` y el número de discrepancias. Parámetro `--size N` (tamaño de la matriz).

## 14.2 Archivo (`file`)

Distribuye un archivo (por defecto uno generado de 1 MB) entre los nodos, calcula un checksum por bloque y lo compara contra el checksum completo esperado (`0xc2dc9dc5`). Imprime `checksum completo = 0x..., OK`. Parámetro `--file RUTA` para usar un archivo real. Si el archivo no existe, reporta `abortado (archivo no disponible)` y código de salida distinto de 0.

## 14.3 Counter (`counter`)

Incrementos concurrentes: cada nodo/hilo incrementa un contador compartido. Al finalizar, el nodo 0 suma todos los contadores y compara contra el esperado (`nodos × hilos × iters`). Imprime `suma = X, esperado = Y, OK`. Parámetros `--iters N` y `--threads N`.

## 14.4 Bench (`bench`)

Mide latencia de acceso local y remoto, y de escritura, sobre páginas compartidas. Imprime latencias en us/op por nodo. Parámetro `--iters N` (número de operaciones).

## 14.5 Evict (`evict`)

Fuerza expulsiones/swap escribiendo más páginas que marcos disponibles. Imprime `verificacion OK` y el número de expulsiones. Parámetros `--pages N --frames M`.

## 14.6 Chat (`chat`)

Aplicación de texto libre interactivo: los mensajes escritos en un nodo se propagan a los demás por el espacio compartido. Comandos: `/nick`, `/help`, `/quit`. Parámetros `--nick`, `--duration`, `--text`, `--serve`.

## 14.7 Stress (`stress`)

Combina `matrices 192` y `counter 100×2` para probar el sistema bajo carga combinada.

## 14.8 Resultados esperados

| App | Salida esperada | Código de salida |
|---|---|---|
| matrices | `verificacion: OK (0 discrepancias)` | 0 |
| file | `checksum completo = 0xc2dc9dc5, OK` | 0 |
| counter | `suma = esperado, OK` | 0 |
| bench | latencias impresas, sin crash | 0 |
| evict | `verificacion OK`, expulsiones > 0 | 0 |
| chat | mensajes cruzan entre nodos | 0 |
| file (archivo inexistente) | `abortado (archivo no disponible)` | ≠ 0 |