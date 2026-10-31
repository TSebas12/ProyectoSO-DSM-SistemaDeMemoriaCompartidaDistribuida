# Entregable 2 — Prototipo 1 y Arquitectura Actualizada

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Integrantes** | José Daniel Monterrosa Quirós · Sebastián Aguero Brenes |
| **Fecha oficial** | Viernes 13 de noviembre de 2026 |
| **Documento a generar** | `Entregable_2/02_Entregable_2_Prototipo_1.md` |
| **Documento base** | `00_Proyecto_DSM_General.md` y `Entregable_1/01_Entregable_1_Diseno.md` |
| **Estado** | Pendiente |
| **Formato** | Markdown académico formal; exportable a Word/PDF si se solicita |

---

## 1. Objetivo

Presentar la **primera implementación** de la arquitectura propuesta en el Entregable 1, junto con la **arquitectura actualizada** y la documentación de todos los cambios o ajustes realizados.

El prototipo 1 no tiene que ser el sistema final, pero sí debe ser una implementación real y ejecutable que demuestre el núcleo del DSM.

---

## 2. Qué pide el PDF

> **Entregable 2.** Prototipo 1 y Arquitectura actualizada. Primera implementación de la arquitectura inicial propuesta y documentación de los cambios o ajustes realizados a la primera versión.

---

## 3. Entregables concretos

Además del documento Markdown, el entregable debe incluir:

- Código fuente del prototipo 1.
- `Makefile`.
- Archivo de configuración de ejemplo.
- `README` con compilación, configuración y ejecución.
- Documento de arquitectura actualizada.
- Documento de cambios respecto al Entregable 1.
- Evidencia de ejecución.
- Repositorio Git con commits de ambos integrantes.

---

## 4. Estructura obligatoria del documento

1. **Portada**
2. **Resumen ejecutivo**
3. **Índice**
4. **Introducción**
   - Objetivo del prototipo 1.
   - Alcance.
   - Relación con el Entregable 1.
5. **Arquitectura actualizada**
   - Diagrama de componentes.
   - Diagrama de despliegue.
   - Cambios respecto al diseño original.
   - Decisiones tomadas durante la implementación.
6. **Cambios y ajustes respecto al Entregable 1**
   - Qué se mantuvo.
   - Qué se cambió.
   - Por qué se cambió.
   - Impacto en API, protocolo, configuración y estructuras.
7. **Implementación del prototipo 1**
   - Módulo de memoria virtual.
   - Módulo de red.
   - Módulo de concurrencia.
   - MSI implementado.
   - API disponible.
   - Configuración.
8. **Estructuras y tipos implementados**
   - Structs reales.
   - Tamaños.
   - Diferencias con el diseño.
9. **Compilación y ejecución**
   - Requisitos.
   - `make`.
   - Configuración.
   - Arranque de nodos.
   - Ejecución de pruebas.
10. **Pruebas realizadas**
    - Escenarios.
    - Resultados.
    - Evidencia.
    - Problemas encontrados.
11. **Limitaciones conocidas**
12. **Trabajo pendiente para el prototipo 2**
13. **Conclusiones**
14. **Referencias**
15. **Anexos**
    - Logs.
    - Capturas.
    - Tablas de resultados.

---

## 5. Qué debe demostrar el prototipo 1

Mínimo:

- Compila en Linux con `make`.
- Arranca al menos **2 nodos**.
- Cada nodo abre su puerto TCP.
- Un nodo se registra con otro.
- Se intercambia al menos un mensaje de heartbeat.
- Se reserva memoria con `mmap`.
- Se configura `PROT_NONE` con `mprotect`.
- Un acceso a página inválida genera `SIGSEGV`.
- El handler captura `si_addr`.
- Se solicita una página por red.
- Se recibe `PAGE_DATA`.
- Se ajustan permisos y el hilo continúa.
- Se demuestra al menos una transición MSI básica.
- Se puede consultar el estado por CLI.

Deseable:

- `INVALIDATE` funcional.
- Varios hilos.
- Varias páginas.
- Estadísticas básicas.
- Web mínima.
- Manejo de timeout.

---

## 6. Evidencia que debe incluir

- Salida de `make`.
- Salida de arranque de 2 nodos.
- Logs de heartbeat.
- Logs de `PAGE_REQUEST` / `PAGE_DATA`.
- Captura o texto de la CLI.
- Prueba de lectura remota.
- Prueba de escritura con invalidación, si está implementada.
- `git log` con commits de ambos integrantes.
- Enlace al repositorio.
- Instrucciones reproducibles.

---

## 7. Checklist de entrega

- [ ] Documento Markdown del prototipo 1.
- [ ] Arquitectura actualizada.
- [ ] Cambios respecto al E1.
- [ ] Código fuente.
- [ ] `Makefile`.
- [ ] `README`.
- [ ] Configuración de ejemplo.
- [ ] Compilación exitosa.
- [ ] Arranque de 2 nodos.
- [ ] Transferencia de página.
- [ ] MSI básico.
- [ ] Evidencia de ejecución.
- [ ] Limitaciones.
- [ ] Trabajo pendiente.
- [ ] Repositorio Git.
- [ ] Commits de ambos integrantes.

---

## 8. Criterios de aceptación

- El prototipo compila y ejecuta en Linux.
- Se demuestra comunicación real entre dos nodos.
- Se demuestra paginación bajo demanda.
- Se demuestra al menos una transición MSI.
- La documentación explica qué cambió respecto al E1.
- El código está modularizado.
- El repositorio tiene commits de ambos integrantes.
- Las instrucciones permiten reproducir la ejecución.

---

## 9. Riesgos típicos de esta etapa

| Riesgo | Mitigación |
|---|---|
| `SIGSEGV` inestable | Handler mínimo + `fault_resolver`; probar primero una página. |
| Deadlocks | Timeouts, locks por página, no esperar red con locks. |
| Protocolo incompleto | Implementar primero `PAGE_REQUEST`, `PAGE_DATA`, `INVALIDATE`. |
| Demasiado alcance | Priorizar 2 nodos y una página antes de escalar. |
| Git con un solo autor | Commits frecuentes de ambos. |
| Demo no reproducible | README y scripts de arranque. |

---

## 10. Próximo paso

Implementar el prototipo 1 y redactar `Entregable_2/02_Entregable_2_Prototipo_1.md` con la arquitectura actualizada y los cambios respecto al Entregable 1.
