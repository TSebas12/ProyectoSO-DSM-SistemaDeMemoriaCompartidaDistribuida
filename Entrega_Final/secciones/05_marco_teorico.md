# 5. Marco teórico

## 5.1 Sistema de Memoria Compartida Distribuida (DSM)

Un DSM (Distributed Shared Memory) es una abstracción que permite que procesos en máquinas distintas compartan un único espacio de memoria lógico, a pesar de que la memoria física está distribuida. El programador accede a la memoria compartida mediante operaciones normales de lectura/escritura sobre punteros; el runtime se encarga de localizar la página que contiene la dirección, traerla si no está presente y mantener la coherencia entre copias.

## 5.2 Paginación bajo demanda

La memoria compartida se divide en **páginas** (en este sistema, de 4 KB). Cada nodo reserva una región de memoria virtual del tamaño total del espacio compartido mediante `mmap` con protección inicial `PROT_NONE`. Cuando el programa accede a una dirección no residente, el hardware genera un `SIGSEGV`; el manejador instalado por el runtime identifica la página, solicita su contenido al nodo que la posee y la instala con `mprotect(PROT_READ|PROT_WRITE)` (o `PROT_READ` según el estado MSI). Este mecanismo evita traer páginas que nunca se usan.

## 5.3 Coherencia MSI

MSI es un protocolo de coherencia de caché con tres estados por bloque (página):

- **M (Modified):** la página fue modificada localmente; es la única copia válida y debe hacerse writeback al home al expulsarla.
- **S (Shared):** la página es de solo lectura y puede existir en varios nodos simultáneamente.
- **I (Invalid):** la página no es válida en este nodo; un acceso genera fallo de página.

Las transiciones se disparan por fallos de página locales y por mensajes de invalidación/remisión provenientes de otros nodos. El directorio (en el nodo home) registra, para cada página, el nodo owner y la lista de sharers.

## 5.4 Señales

El runtime usa `sigaction` para instalar un manejador de `SIGSEGV` (y `SIGBUS`). El manejador corre en el contexto del hilo que causó el fallo y delega la resolución a `fault_resolver`, que determina la página, su estado y la acción a tomar (traer, invalidar, writeback). Se evita hacer trabajo pesado dentro del manejador para no violar las restricciones de async-signal-safety.

## 5.5 Sockets

La comunicación entre nodos se realiza por TCP/IP. Cada nodo abre un socket de escucha en su puerto DSM y mantiene conexiones persistentes con los demás nodos (pool de conexiones). Los mensajes tienen una cabecera fija de 64 bytes y un payload variable. El hilo de red (`net_thread`) atiende las conexiones entrantes y despacha los mensajes a los handlers correspondientes.

## 5.6 Pthreads y semáforos

La concurrencia interna se gestiona con Pthreads (hilos de red, de heartbeat, de aplicación), variables de condición, semáforos POSIX y operaciones atómicas (`_Atomic`, `atomic_fetch_add`). La barrera centralizada sincroniza a todos los nodos participantes antes de iniciar una fase de una aplicación.

## 5.7 Heartbeat

Cada nodo envía periódicamente un `PING` a los demás nodos y actualiza su tabla de estado (`alive[]`, `last_seen_ms[]`). Si un nodo no responde dentro del timeout configurado, se marca como caído y se notifica a la capa de fallos. El heartbeat se ejecuta en un hilo dedicado (`heartbeat_thread`).

## 5.8 LRU y swap

La política de reemplazo es LRU aproximada basada en un bit de referencia por marco. Cuando se necesita un marco libre y no hay, se elige la página menos recientemente usada, se hace writeback si está en estado M y se guarda en swap local (solo en el home). Las páginas expulsadas se marcan como no residentes.