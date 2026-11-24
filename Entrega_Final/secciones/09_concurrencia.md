# 9. Concurrencia entre señales y sockets

## 9.1 Modelo de concurrencia

Cada nodo ejecuta varios hilos concurrentes:

- Hilo principal (inicialización y aplicación).
- Listener + pool de 32 workers (red).
- Hilo resolver (fallos de página).
- Hilo heartbeat.
- Hilos de la aplicación (`dsm_attach`/`dsm_detach`).

La concurrencia se gestiona con Pthreads, mutex/cond con clases y verificación de orden, variables `_Atomic` y una cola lock-free para fallos.

## 9.2 `SIGSEGV` y `sigaction`

- El manejador se instala con `sigaction` y `SA_SIGINFO`, para recibir `si_addr`.
- Solo se permite async-signal-safe dentro del handler: encolar la página en la cola lock-free y suspender el hilo con `sigsuspend`.
- El trabajo pesado (RPC, asignación de marcos, instalación) se hace en el hilo resolver, no en el handler.

## 9.3 `fault_resolver`

- Un hilo dedicado consume la cola de fallos.
- Invoca `dsm_coherence_handle_fault` para resolver la página (local o remota).
- Reanuda al hilo de usuario con `pthread_kill(DSM_SIG_RESUME)`.
- El acceso se reintenta; si la página ya está residente, continúa.

## 9.4 Hilo de red

- Listener acepta conexiones y las despacha al pool de 32 workers.
- Cada worker lee el mensaje completo, despacha a coherencia y responde por el mismo socket.
- Las RPC salientes hacia un mismo peer se serializan con un mutex por peer.
- El receptor atiende varias conexiones en paralelo.

## 9.5 Locks y semáforos

- Mutex por clase: `GLOBAL` (op_lock), `OBJECT` (table_lock), `PAGE` (page->lock), `FRAME` (frame_lock).
- Verificación de orden en runtime (`check_order`).
- Variables `_Atomic` para contadores y estado de heartbeat (`alive[]`).
- Semáforos POSIX donde aplica.

## 9.6 Concurrencia señales vs sockets

- El handler de `SIGSEGV` no toca sockets ni locks no async-signal-safe.
- La cola lock-free desacopla el handler del resolver.
- El resolver puede hacer RPCs y tomar locks sin restricción de señal.
- `SIGPIPE` se ignora (`SIG_IGN`) para no morir al escribir en un socket cerrado.

## 9.7 Reglas de async-signal-safety

- Dentro del handler: solo encolar y suspender.
- Fuera del handler: todo lo demás (memoria, red, locks, logs).
- `setvbuf(stdout, NULL, _IOLBF, 0)` para que un `SIGTERM` no pierda el búfer.

---

*Fin de la sección 9.*
