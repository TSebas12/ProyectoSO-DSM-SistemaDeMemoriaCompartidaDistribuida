# 4. Introducción

## 4.1 Problema

En un sistema de memoria compartida tradicional, todos los procesos acceden a un mismo espacio de memoria física gestionado por el sistema operativo. Cuando los procesos residen en máquinas distintas, no existe un espacio de memoria compartido de forma nativa: cada nodo tiene su propia memoria y la comunicación se realiza por mensajes. Esto obliga al programador a repartir datos y sincronizar explícitamente, lo que complica el desarrollo de aplicaciones paralelas y distribuidas.

El problema que resuelve este proyecto es: **cómo ofrecer a los procesos de varios nodos una única región de memoria lógicamente compartida**, de manera que el programador use punteros normales (`dsm_malloc`/`dsm_free`) mientras el runtime se encarga de traer las páginas bajo demanda, mantener la coherencia entre copias y sincronizar los nodos.

## 4.2 Objetivos

### 4.2.1 Objetivo general

Implementar un Sistema de Memoria Compartida Distribuida (DSM) en espacio de usuario, sobre Linux, con paginación bajo demanda y coherencia a nivel de página mediante el protocolo MSI, capaz de ejecutar aplicaciones reales sobre 2 a 4 nodos y demostrarse en vivo.

### 4.2.2 Objetivos específicos

1. Diseñar e implementar un runtime DSM modular (memoria virtual, red, protocolo, coherencia, sincronización, fallos, CLI, web) en C11.
2. Proveer la API pública `dsm_malloc`/`dsm_free`/`dsm_barrier`/`dsm_node_id`/`dsm_node_count` para las aplicaciones.
3. Implementar la coherencia MSI con un directorio por página en el nodo home y transferencia/invalidación/writeback por TCP.
4. Implementar la paginación bajo demanda con `mmap`, `mprotect` y manejador `SIGSEGV`.
5. Implementar sincronización entre hilos y nodos (Pthreads, semáforos POSIX, atómicos, barrera centralizada).
6. Implementar expulsión LRU aproximada con swap local en el home.
7. Desarrollar aplicaciones de prueba: matrices, archivo, counter, bench, evict y chat.
8. Documentar el sistema (informe técnico, manual de usuario) y demostrarlo en vivo.

## 4.3 Alcance

### 4.3.1 Incluido

- Hasta 16 nodos configurables (ideal 4, mínimo 2 para pruebas).
- Páginas de 4 KB, memoria por nodo configurable (por defecto 1 MB).
- Coherencia MSI a nivel de página.
- Expulsión LRU aproximada con swap local en el home.
- Heartbeat y detección de nodos caídos.
- Interfaz web de observación y CLI `dsmctl`.
- Aplicaciones de prueba reales.

### 4.3.2 Excluido (trabajo futuro)

- Ejecución distribuida de procesos (Extra 1): no implementado; documentado como diseño.
- Tolerancia a fallos por replicación (Extra 2): no implementado; `replication_factor = 0`.
- Despliegue multi-máquina con IPs reales: pendiente de la versión final; las pruebas se realizan en `127.0.0.1`.
- Persistencia de checkpoint: `checkpoint_interval_ms = 0`.

## 4.4 Estructura del documento

El documento se organiza en 21 secciones: portada, resumen ejecutivo, índice, introducción, marco teórico, arquitectura final, módulos implementados, diagrama de estados MSI, concurrencia, interbloqueos, estructuras y archivos, API, protocolo, aplicaciones de prueba, pruebas de uso y ejecución, rendimiento, extras, manual de usuario, conclusiones, referencias y anexos.