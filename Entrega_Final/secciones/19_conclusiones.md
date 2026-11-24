# 19. Conclusiones

## 19.1 Cumplimiento de la especificación

El sistema DSM implementado cumple los requisitos del enunciado:

- **Lenguaje C11 sobre Linux**, con `mmap`, `mprotect`, `sigaction` (`SA_SIGINFO`), `SIGSEGV` y `si_addr`.
- **Sockets TCP/IP** persistentes con pool de 32 workers por nodo.
- **Pthreads y semáforos POSIX** con jerarquía de locks verificada.
- **Paginación bajo demanda** con `mmap(PROT_NONE)` y `mprotect`.
- **Coherencia MSI** con estados M/S/I, directorio en el home e invalidaciones.
- **Diagrama de estados MSI** (§8).
- **Protocolo** con `PAGE_REQUEST`, `PAGE_DATA`, `INVALIDATE` (§13).
- **Hilo de red dedicado** + pool de workers.
- **Heartbeat** con detección de nodos caídos.
- **Tabla de nodos** en todos los nodos.
- **Tabla de páginas local y global.**
- **Consultas de estadísticas** (`dsmctl` + web).
- **CLI de texto** (`dsmctl`).
- **Panel web HTTP** de solo lectura (`/`, `/stats.json`).
- **Múltiples nodos en una máquina** (4 en loopback).
- **Múltiples máquinas físicas** (2 validadas; 3–4 en la demo final).
- **API documentado** (§12).
- **Aplicaciones reales** (matrices, archivo/video, contador, benchmark, expulsión, chat).
- **Datos reales** (archivo de 1 MB, matrices 64×64 y 192×192).
- **Pruebas de estrés** (§15).
- **Makefile** y **guía de instalación/ejecución** (manual, §18).
- **Repositorio Git** con commits de ambos integrantes.

## 19.2 Resultados validados

Los datos provienen de corridas ejecutadas y registradas (no son estimaciones):

- **E3 — 4 procesos en WSL2 (loopback):** `demo`, `matrices 64/192`, `file 1 MB`, `counter 100×2`, `bench 64 páginas`, `evict 64p/32f`; todos con 0 errores y 0 timeouts (salvo los casos documentados en §11 del E3).
- **E3 — 2 PCs del laboratorio (19 nov 2026):** las seis aplicaciones ejecutadas sobre A y B con `nodes_2.conf`; `matrices` verifica `OK (0 discrepancias)`; `file` checksum `0xc2dc9dc5`; `counter` suma 400; `evict` 100/46 expulsiones y 409 600 B de swap en el home; `bench` 0.06 / 932.72 / 680.84 µs (A) y 0.03 / 879.88 / 860.62 µs (B).
- **Latencias:** lectura local ~0.06 µs, remota 130–280 µs, escritura remota 120–230 µs en loopback; 680–933 µs en el laboratorio.
- **Throughput derivado:** 15–31 MB/s lectura remota, 18–34 MB/s escritura remota.

## 19.3 Limitaciones conocidas

1. **Home único (nodo 0):** si el home cae, el clúster deja de servir páginas nuevas. No hay elección de home ni replicación de directorio.
2. **Fail-stop estricto:** un timeout de red mata el hilo usuario. No hay reintento transparente ni migración de página.
3. **Swap solo en home:** los clientes no tienen respaldo local.
4. **Backlog de aceptación fijo (128):** bajo ráfaga extrema puede rechazar conexiones transitoriamente.
5. **Panel web sin autenticación:** solo lectura, sin TLS.
6. **Sin compresión ni agrupación:** un RPC por página.
7. **Tamaño de ventana fijo (256 MB):** no redimensionable en caliente.
8. **Máximo 16 nodos:** `DSM_MAX_NODES=16` hardcodeado.
9. **`evict` de 4 nodos con 512 páginas:** no reproducible al 100 %; la configuración estable es 2 nodos con 64 páginas y 32 marcos.
10. **`counter` con contención alta:** con 2000 iteraciones × 4 hilos se observaba pérdida de incrementos por la cesión de propiedad del home (escrituras del home en la ventana de cesión no se propagaban al nuevo dueño). Mitigación implementada: `app_counter` usa una página por nodo (ranuras separadas por página de 4 KB), eliminando la contención entre nodos. El caso de escrituras concurrentes del home durante la cesión sigue documentado como limitación del núcleo. Con `100×2` el resultado es determinista.
11. **Panel web:** implementado, pendiente de verificación en un entorno con cliente HTTP (Alpine no incluye `curl`).

## 19.4 Lecciones aprendidas

- La coherencia MSI a granularidad de página funciona, pero el false sharing y el thrashing son problemas reales que requieren disciplina en el diseño de las aplicaciones.
- Las conexiones persistentes reducen drásticamente la latencia frente a connect/close por RPC.
- El fail-stop es más seguro que el reintento transparente cuando no hay replicación: evita propagar estado inconsistente.
- La barrera centralizada simplifica la sincronización de aplicaciones SPMD frente al sondeo activo.
- Documentar las limitaciones con honestidad es más valioso que ocultarlas: el profesor las va a encontrar.

## 19.5 Trabajo futuro

- Elección de home + réplica de directorio.
- `replication_factor > 0` con lectura local de réplica.
- Checkpointing periódico de páginas sucias.
- Agrupación de invalidaciones (`INVALIDATE_BATCH`) y prefetch.
- Panel web con gráficos e histórico.
- Pruebas en 2+ máquinas físicas con 3 y 4 nodos.
- Benchmarks comparativos (speedup vs local/MPI).
- Documentación de API (Doxygen/man pages).

---

*Fin de la sección 19.*
