# 15. Pruebas de uso y ejecución

## 15.1 Escenarios

| Escenario | Nodos | App | Objetivo |
|---|---|---|---|
| S1 | 2 | demo | Humo: página compartida y barrera = 3 |
| S2 | 2 | matrices 64 | Coherencia básica |
| S3 | 2 | matrices 192 | Coherencia con carga |
| S4 | 2 | file | Checksum distribuido |
| S5 | 2 | counter 20×2 | Incrementos sin pérdida |
| S6 | 2 | bench 2000 | Latencias |
| S7 | 2 | evict 64p/32m | Expulsión y swap |
| S8 | 2 | chat | Mensajes cruzados |
| S9 | 2 | stress | Carga combinada |
| S10 | 4 (loopback) | matrices 128 | Escalabilidad |
| S11 | 4 (loopback) | counter 2000×4 | Escalabilidad |

## 15.2 Procedimiento

1. Compilar: `make clean && make`.
2. Generar topología con `./dsm.sh` (opción 1) y validar (opción 11): huella idéntica en todos los nodos.
3. Arrancar nodo 0 (home) y luego los demás, esperando `listo`.
4. Ejecutar la app y capturar salida de cada nodo + `stats.json`.
5. Detener con `pkill -f dsm_node`.

## 15.3 Resultados medidos

| Escenario | Resultado | Tiempo | Observaciones |
|---|---|---|---|
| S1 demo | OK (fallos 2/2, reenvios 1, barreras 3, errores 0) | ~2 s | OK |
| S2 matrices 64 | OK (0 discrepancias, fallos 20/20, errores 0) | ~60 s | OK |
| S3 matrices 192 | no ejecutado en esta corrida | - | pendiente |
| S4 file | OK (checksum 0xc2dc9dc5, 2 bloques, fallos 262/130, errores 0) | ~60 s | OK |
| S5 counter 100×2 | OK (suma 400 = esperado, fallos 4/3, errores 0) | ~2 s | OK |
| S6 bench 2000 | OK (latencias impresas, errores 0) | ~2 s | OK |
| S7 evict 64p/32m | OK (expulsiones 64, residentes 32, suma 2048, errores 0) | ~60 s | OK |
| S8 chat | OK (mensajes cruzan: [nodo 1] bob: hola desde bob) | ~8 s | OK |
| S9 stress | no ejecutado en esta corrida | - | pendiente |
| S10 matrices 64 (4n) | OK (0 discrepancias x4, fallos 18 c/u, errores 0) | ~60 s | OK |
| S11 counter 2000×4 (4n) | no ejecutado en esta corrida | - | pendiente |

## 15.4 Problemas encontrados y resueltos

- **Arranque desfasado:** si los nodos arrancan con varios segundos de diferencia, el heartbeat reporta errores/reintentos aunque ambos estén vivos. Mitigación: arrancar A, esperar `listo`, luego B.
- **Barrera expirada:** si una barrera expira, la app ahora aborta en lugar de reportar OK con datos incompletos.
- **Handoff de ownership (4 nodos simultáneos):** resuelto conservando la copia del home hasta confirmar la instalación en el solicitante.
- **Chat con stdin cerrado:** corregido para no entrar en bucle.

## 15.5 Evidencia

Las capturas de salida, logs (`logs/dsm-nodeN.log`) y `stats.json` se incluyen en el anexo 21.