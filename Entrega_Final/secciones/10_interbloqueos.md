# 10. Interbloqueos y su resolución

## 10.1 Fuentes de interbloqueo

En un DSM con memoria compartida distribuida, los interbloqueos pueden aparecer en tres niveles:

1. **Locks de página y directorio:** si un hilo toma `op_lock` y luego espera un lock de página, mientras otro toma el lock de página y espera `op_lock`, se produce un deadlock clásico.
2. **RPCs anidadas:** si un nodo A espera respuesta de B mientras B espera respuesta de A, ambos se bloquean.
3. **Barreras:** si un nodo no llega a la barrera, los demás esperan hasta el timeout.

## 10.2 Jerarquía de locks

Los locks siguen un orden fijo y verificado en runtime por clases (`dsm_mutex_init` con `DSM_LOCK_GLOBAL/OBJECT/PAGE/FRAME`):

```
op_lock (GLOBAL) → table_lock (OBJECT) → page->lock (PAGE) → frame_lock (FRAME)
```

Nunca se toma un lock de clase menor mientras se tiene uno de clase mayor. Esta regla se revisa en code review y se valida con `check_order`.

## 10.3 Caso real resuelto: `local_drop` con `op_lock` tomado

Durante la revisión del prototipo 2 se detectó que `home_serve_write` llamaba a `local_drop` mientras tenía tomado el `op_lock` del directorio. `local_drop` toma el lock de la página (`p->lock`) y puede esperar hasta 200 ms por una operación de expulsión. El orden resultante era:

```
op_lock → page->lock   (en home_serve_write → local_drop)
page->lock → op_lock   (en el hilo de fallo, vía local_fetch → dir_lock_entry)
```

Es el patrón clásico de deadlock por orden inverso. No se reprodujo de forma determinista en las pruebas (la espera está acotada a 200 ms y la página suele estar disponible), pero el riesgo es real y afecta la latencia: otros hilos esperan hasta 200 ms por el `op_lock` de esa página.

**Resolución implementada en la versión final:** `home_serve_write` libera el `op_lock` **antes** de llamar a `local_drop`, y los servicios (`home_serve_read`, `home_serve_write`) son ahora los responsables de liberar el `op_lock` en todas sus rutas de salida. Los llamadores (`local_fetch` y los handlers de `PAGE_READ`/`PAGE_WRITE`) ya no lo liberan, evitando un desbloqueo doble. `local_drop` se ejecuta sin `op_lock` tomado, eliminando la inversión de orden y la espera de hasta 200 ms con el lock tomado.

## 10.4 Timeouts

- `network_timeout_ms = 10000`: timeout de una RPC.
- `connect_timeout_ms = 5000`: timeout de conexión.
- `heartbeat_timeout_ms = 3000`: timeout de heartbeat.
- `barrier_timeout_ms = 30000`: timeout de barrera (la demo usa 120 000 ms).
- `retry_count = 5` con `retry_backoff_ms = 500`.

Si una RPC falla tras los reintentos, el hilo usuario recibe `SIGSEGV` y muere (fail-stop). No se propaga estado inconsistente.

## 10.5 Orden de locks en la red

- Las RPC salientes hacia un mismo peer se serializan con un mutex por peer (`peer_acquire`/`peer_release`).
- El receptor atiende varias conexiones en paralelo con su pool de 32 workers.
- Nunca se espera una respuesta de red con un lock de página o de directorio tomado (regla verificada en code review).

## 10.6 Reintentos e idempotencia

- Cada mensaje lleva `request_id` para detectar duplicados.
- Los mensajes con `page_id` inválido se rechazan.
- Un mensaje truncado no corrompe el estado: se descarta y se cierra la conexión.
- El protocolo tolera mensajes duplicados o fuera de orden razonablemente.

## 10.7 Pruebas de interbloqueo

- 4 nodos con `matrices 192` durante 30 minutos: sin cuelgues.
- `evict 64/32` con 2 nodos: sin cuelgues.
- Barrera con un nodo que no arranca: timeout a los 120 s con mensaje claro.
- Cambio de configuración en un nodo: error explícito, no bloqueo.

## 10.8 Resumen de mitigaciones

| Riesgo | Mitigación |
|---|---|
| Locks en orden inverso | Jerarquía fija + clases + revisión |
| RPCs anidadas | Fail-stop; no esperar con locks |
| Barrera que no llega | Timeout configurable + mensaje claro |
| Esperas largas con lock | Liberar lock antes de esperar (local_drop) |
| Conexiones saturadas | Pool de 32 workers + backlog 128 |

---

*Fin de la sección 10.*
