# 2. Resumen ejecutivo

Este documento presenta el diseño, la implementación y la validación de un **Sistema de Memoria Compartida Distribuida (DSM)** en espacio de usuario, escrito en C11 sobre Linux. El sistema permite que varios nodos independientes, conectados por TCP/IP, aporten memoria física y formen un espacio de memoria lógicamente compartido que las aplicaciones usan mediante punteros normales (`dsm_malloc`/`dsm_free`).

El runtime se encarga de traer páginas bajo demanda con `mmap`, `mprotect` y `SIGSEGV`; de mantener la coherencia entre copias con el protocolo **MSI** (Modified, Shared, Invalid) y un directorio distribuido por página en el nodo home; de transferir, invalidar y hacer writeback de páginas por la red con conexiones TCP persistentes y un pool de 32 workers; de sincronizar hilos y nodos con Pthreads, semáforos POSIX, variables atómicas y una barrera centralizada; y de expulsar páginas con política LRU aproximada y swap local en el home.

Se implementaron y validaron seis aplicaciones de prueba (`demo`, `matrices`, `file`, `counter`, `bench`, `evict`) y una séptima (`chat`) para texto libre entre nodos. Las pruebas cubren 2 PCs físicas del laboratorio y 4 procesos en loopback, con latencias de lectura remota de 130–280 µs y escritura remota de 120–230 µs en loopback, y 680–933 µs en el laboratorio.

Se documentan las limitaciones conocidas (home único, fail-stop estricto, swap solo en home, backlog fijo, web sin autenticación, ventana fija, máximo 16 nodos, inestabilidad de `evict` con 4 nodos y 512 páginas, pérdida de incrementos de `counter` con contención alta) y el trabajo futuro (replicación, checkpointing, elección de home, agrupación de invalidaciones, panel web enriquecido).

El repositorio Git registra commits de ambos integrantes y el historial de evolución desde el Entregable 1.

---

*Fin de la sección 2.*
