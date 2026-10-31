# Entregable 1 — Documento de Diseño del Sistema DSM

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Integrantes** | José Daniel Monterrosa Quirós · Sebastián Aguero Brenes |
| **Fecha oficial** | Viernes 6 de noviembre de 2026 |
| **Documento a generar** | `Entregable_1/01_Entregable_1_Diseno.md` |
| **Documento base** | `00_Proyecto_DSM_General.md` |
| **Estado** | Pendiente de redacción |
| **Formato** | Markdown académico formal; exportable a Word/PDF si se solicita |

---

## 1. Objetivo

Producir el **documento de diseño** del sistema DSM antes de implementarlo. Debe demostrar que el equipo entiende el problema, la arquitectura, el modelo de coherencia, el API, el protocolo y la configuración, con el nivel de detalle suficiente para que otra persona pueda implementar el sistema a partir del documento.

Este entregable es **documental**: no requiere código funcionando.

---

## 2. Qué pide el PDF

El enunciado indica textualmente:

> **Entregable 1.** Documento de Diseño del sistema DSM. Arquitectura general, borrador de API inicial, protocolo de mensajes, datos de configuración. Incluya detalles de implementación tales como estructuras y tipos de datos, tamaños de los datos, etc.

Por lo tanto, el documento debe incluir como mínimo:

- Arquitectura general.
- Borrador de API inicial.
- Protocolo de mensajes.
- Datos de configuración.
- Estructuras y tipos de datos.
- Tamaños de los datos.
- Detalles de implementación.

---

## 3. Estructura obligatoria del documento

1. **Portada**
   - Institución, escuela, curso, semestre, profesor.
   - Título del proyecto.
   - Integrantes.
   - Fecha y versión.
2. **Resumen ejecutivo**
3. **Índice de contenido, figuras y tablas**
4. **Introducción**
   - Problema.
   - Motivación.
   - Objetivos general y específicos.
   - Alcance y supuestos.
5. **Marco teórico**
   - DSM.
   - Paginación bajo demanda.
   - `mmap`, `mprotect`, `SIGSEGV`, `sigaction`.
   - Sockets TCP/IP.
   - Pthreads y semáforos POSIX.
   - Protocolo MSI.
6. **Arquitectura general**
   - Nodos, componentes y despliegue.
   - Modelo de memoria global (pool de marcos).
   - Administración distribuida.
   - Membresía, registro de nodos y heartbeat.
   - Ciclo de vida de un nodo.
   - Hilos internos.
7. **Modelo de coherencia MSI**
   - Estados `M`, `S`, `I`.
   - Invariantes.
   - Tabla de transiciones.
   - Diagrama de estados.
   - Directorio de coherencia.
   - Manejo de false sharing, ping-pong y thrashing.
8. **Diseño del Módulo 1 — Memoria virtual y paginación bajo demanda**
   - Reserva con `mmap`.
   - Permisos con `mprotect`.
   - Handler `SIGSEGV` con `sigaction` y `SA_SIGINFO`.
   - `si_addr`, cálculo de página, pausa del hilo, resolución.
   - `fault_resolver` y seguridad de señales.
9. **Diseño del Módulo 2 — Red y protocolo**
   - Puerto de escucha.
   - `Network Listener Thread`.
   - Conexiones, framing, timeouts.
   - Catálogo de mensajes.
   - Formato de cabecera y payloads.
   - Flujos de lectura, escritura, invalidación y heartbeat.
10. **Diseño del Módulo 3 — Concurrencia y sincronización**
    - Hilos de usuario.
    - Tablas de páginas y listas de copias.
    - Mutex y semáforos POSIX.
    - Jerarquía de locks.
    - Condiciones de carrera.
    - Prevención de deadlocks.
11. **Estructuras de datos, tipos y tamaños**
    - Tabla de nodos.
    - Tabla de páginas.
    - Entrada de directorio.
    - Pool de marcos.
    - Estadísticas.
    - Structs del protocolo.
    - Tamaños en bytes, alineamiento y serialización.
12. **API del DSM**
    - Tipos.
    - Funciones.
    - Parámetros.
    - Entradas.
    - Salidas.
    - Errores.
    - Ejemplos de uso.
13. **Protocolo de mensajería**
    - Cabecera.
    - Catálogo completo.
    - Campos y tamaños.
    - Semántica.
    - Respuestas.
    - Errores.
    - Idempotencia, timeouts y versionado.
14. **Configuración del sistema**
    - Archivo compartido.
    - Tabla de parámetros.
    - Tipos, unidades, defaults y rangos.
    - Ejemplo completo.
    - Validación.
    - Puertos por pareja.
15. **Aplicaciones de prueba**
    - Matrices.
    - Archivo/video.
    - Incrementos concurrentes como estrés.
    - Adaptación al API.
16. **Plan de pruebas y métricas**
17. **Plan de trabajo, riesgos y decisiones**
18. **Conclusiones**
19. **Referencias**
20. **Anexos**
    - Glosario.
    - Tablas auxiliares.
    - Cálculos de tamaños.
    - Decisiones pendientes.

---

## 4. Detalle mínimo que no puede faltar

### 4.1 Arquitectura

- Diagrama de componentes.
- Diagrama de despliegue.
- Explicación de cómo se suma la memoria de los nodos.
- Explicación de cómo se localizan las páginas.
- Explicación de qué pasa cuando un nodo se une o se cae.

### 4.2 MSI

- Diagrama de estados.
- Tabla de transiciones con eventos y acciones.
- Explicación de quién invalida, quién concede propiedad y quién responde.
- Explicación del directorio y del `home_node`.

### 4.3 API

- Listado completo de funciones.
- Firma de cada función.
- Parámetros de entrada.
- Valores de salida.
- Códigos de error.
- Ejemplo de uso.
- Semántica de memoria compartida, sincronización y liberación.

### 4.4 Protocolo

- Cabecera binaria con campos y tamaños.
- Mensajes mínimos: `PAGE_REQUEST`, `PAGE_DATA`, `INVALIDATE`.
- Mensajes adicionales: join, heartbeat, ownership, ACK, stats, etc.
- Formato de payload.
- Flujos de secuencia.
- Timeouts, reintentos, idempotencia y errores.

### 4.5 Configuración

- Archivo `dsm.conf`.
- Todos los parámetros.
- Valores por defecto.
- Unidades.
- Rangos válidos.
- Ejemplo.
- Validación.

### 4.6 Estructuras y tamaños

- Cada struct con sus campos.
- Tipo de cada campo.
- Tamaño en bytes.
- Alineamiento.
- Cómo se serializa en red.
- Límites: máximo de nodos, máximo de páginas, tamaño de página, tamaño de mensaje.

---

## 5. Formato académico

- Markdown con encabezados numerados.
- Portada.
- Índice.
- Tablas para estructuras, mensajes, parámetros y errores.
- Diagramas Mermaid para arquitectura, estados, secuencias y despliegue.
- Figuras y tablas numeradas y referenciadas.
- Código de ejemplo solo para tipos, firmas y structs; no implementación completa.
- Tono formal.
- Referencias al final.
- Numeración de páginas si se exporta a PDF.

---

## 6. Checklist de entrega

- [ ] Portada completa.
- [ ] Integrantes correctos.
- [ ] Fecha y versión.
- [ ] Índice.
- [ ] Introducción y objetivos.
- [ ] Arquitectura general con diagramas.
- [ ] Modelo de memoria global.
- [ ] Administración distribuida y heartbeat.
- [ ] MSI con estados, invariantes, tabla y diagrama.
- [ ] Módulo 1: `mmap`, `mprotect`, `SIGSEGV`, `sigaction`.
- [ ] Módulo 2: red, listener, protocolo.
- [ ] Módulo 3: concurrencia, locks, semáforos, deadlocks.
- [ ] Estructuras, tipos y tamaños.
- [ ] API completa.
- [ ] Protocolo completo.
- [ ] Configuración completa.
- [ ] Aplicaciones de prueba.
- [ ] Plan de pruebas.
- [ ] Riesgos y decisiones.
- [ ] Conclusiones.
- [ ] Referencias.
- [ ] Anexos.

---

## 7. Criterios de aceptación

- Un lector técnico entiende qué se va a construir.
- Se explica cómo se comunican los nodos.
- Se explica cómo se mantiene la coherencia MSI.
- Se identifican todas las estructuras y sus tamaños.
- El API está completo y documentado.
- El protocolo está completo y documentado.
- La configuración está completa y validada.
- El documento tiene formato académico formal.
- No hay contradicciones entre arquitectura, API, protocolo y configuración.

---

## 8. Pendientes que afectan este entregable

| Tema | Estado | Acción |
|---|---|---|
| URL del repositorio Git | Pendiente | Agregar cuando se conozca. |
| Equipos del laboratorio | Pendiente | No bloquea el diseño; se documenta como supuesto. |
| Cita de demo | Pendiente | No bloquea el E1. |
| Dataset de las apps | Pendiente | Se puede dejar como propuesta. |
| Manual de usuario separado | Pendiente | Se decide para la entrega final. |

---

## 9. Qué no incluye este entregable

- Código fuente funcional.
- Makefile.
- Prototipo ejecutable.
- Resultados de pruebas reales.
- Demo en vivo.

Eso corresponde a los entregables 2, 3 y final.

---

## 10. Próximo paso

Redactar `Entregable_1/01_Entregable_1_Diseno.md` usando como base `00_Proyecto_DSM_General.md`, con el formato académico formal y todos los detalles de estructuras, tipos y tamaños.
