# 17. Extras

## 17.1 Decisión sobre extras

Los extras no son obligatorios, pero cada uno agrega hasta 10 %. La decisión del equipo es: **intentar ambos si el sistema base queda estable**, priorizando el Extra 1 y luego el Extra 2. Si un extra compromete la estabilidad, se desactiva por configuración y se documenta como trabajo futuro.

Estrategia:

1. Primero la base: E1, E2 y E3 deben entregar un DSM funcional, coherente y demostrable.
2. Extra 1 después del E3: la ejecución distribuida de procesos es más natural sobre la arquitectura ya existente.
3. Extra 2 al final: la tolerancia a fallos exige réplicas, recuperación, consistencia y documentación de overhead.
4. Documentar siempre: qué se implementó, qué no, por qué, nivel de tolerancia y costo.
5. No sacrificar la demo base.

## 17.2 Extra 1 — Ejecución distribuida de procesos (10 %)

**Qué pide:** que desde un nodo se coordine la ejecución de procesos paralelos en otros nodos, usando la memoria distribuida en todo el sistema, pero procesando en múltiples nodos separados de forma local y paralela.

**Diseño propuesto:**

- Un nodo coordinador recibe una tarea.
- Divide la tarea en unidades.
- Envía `TASK_SUBMIT` a nodos trabajadores (o lanza por SSH).
- Cada trabajador ejecuta localmente y escribe resultados en DSM.
- El coordinador recolecta y verifica.
- Se usa `dsm_barrier` o `TASK_RESULT` para sincronizar.
- Se documenta el modelo de scheduling, balanceo y tolerancia a fallos.

**Métricas:** speedup, balance de carga, overhead de coordinación, throughput, escalabilidad.

**Riesgos:** complejidad adicional, necesidad de un planificador, sincronización de tareas, manejo de fallos de trabajadores.

**Estado:** IMPLEMENTADO. Se agrego la aplicacion `task` (Extra 1) que ejecuta una tarea distribuida: el coordinador (nodo 0) prepara un arreglo compartido, cada nodo procesa su porcion en paralelo y publica su resultado parcial en DSM; el coordinador recolecta, verifica contra una referencia secuencial y reporta speedup y balance de carga. Se ejecuta con `--app task --size N`. Metricas impresas: speedup, balance, tiempo secuencial vs paralelo.

**Estado previo (diseño):** implementacion opcional, time-boxed, sujeta al gate del miercoles 25 de noviembre.ón opcional, time-boxed, sujeta al gate del miércoles 25 de noviembre. Si no se completa, se documenta el diseño y el motivo.

## 17.3 Extra 2 — Tolerancia a fallos y redundancia (10 %)

**Qué pide:** considerar la seguridad del sistema ante fallas o caída de nodos o de la conexión de red, con cierto nivel de tolerancia para mantener la integridad y no perder información ante la ausencia esporádica de un nodo.

**Diseño propuesto:**

- Réplica de páginas `M` en uno o más nodos.
- `replication_factor` configurable (ya existe en `dsm_config_t`).
- `REPLICA_WRITE` y `REPLICA_ACK`.
- Detección de fallo por heartbeat.
- Promoción de réplica a owner.
- Recuperación de páginas.
- Checkpoints periódicos.
- Log de transiciones.

**Nivel de tolerancia (diseño):**

| `replication_factor` | Nodos que se pueden perder | Condición |
|---|---|---|
| 0 | 0 | Estado actual |
| 1 | 1 | Réplica de cada página M en otro nodo |
| 2 | 2 | Dos réplicas; mayor overhead |

> Aclaración: si cae el home, la tolerancia sigue limitada salvo que se replique también el directorio.

**Overhead (estimado):** espacio `replication_factor * tamaño de datos`; red adicional por escrituras de réplica; CPU por coordinación; latencia por ACK de réplica.

**Estado:** diseño documentado, no implementado. Una tolerancia a fallos mal implementada puede ser peor que no tenerla.

---

*Fin de la sección 17.*
