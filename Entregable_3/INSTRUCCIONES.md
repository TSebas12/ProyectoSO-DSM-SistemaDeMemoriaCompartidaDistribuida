# Entregable 3 — Prototipo 2 y Arquitectura Actualizada

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Integrantes** | José Daniel Monterrosa Quirós · Sebastián Aguero Brenes |
| **Fecha oficial** | Viernes 20 de noviembre de 2026 |
| **Documento a generar** | `Entregable_3/03_Entregable_3_Prototipo_2.md` |
| **Documento base** | `00_Proyecto_DSM_General.md`, E1 y E2 |
| **Estado** | Pendiente |
| **Formato** | Markdown académico formal; exportable a Word/PDF si se solicita |

---

## 1. Objetivo

Presentar la **implementación mejorada** del prototipo inicial, junto con la **arquitectura actualizada** y la documentación de todos los cambios o ajustes realizados para esta segunda versión.

El prototipo 2 debe ser una versión más robusta, concurrente, medible y cercana al sistema final.

---

## 2. Qué pide el PDF

> **Entregable 3.** Prototipo 2 y Arquitectura actualizada. Implementación mejorada del prototipo inicial propuesto y documentación de los cambios o ajustes realizados para esta segunda versión.

---

## 3. Entregables concretos

Además del documento Markdown, el entregable debe incluir:

- Código fuente del prototipo 2.
- `Makefile`.
- Configuración de ejemplo.
- `README` actualizado.
- Aplicaciones de prueba adaptadas al API DSM.
- Documento de arquitectura actualizada.
- Documento de cambios respecto al prototipo 1.
- Pruebas de estrés y resultados.
- Métricas.
- Repositorio Git actualizado.

---

## 4. Estructura obligatoria del documento

1. **Portada**
2. **Resumen ejecutivo**
3. **Índice**
4. **Introducción**
   - Objetivo del prototipo 2.
   - Alcance.
   - Relación con el prototipo 1.
5. **Arquitectura actualizada**
   - Diagrama de componentes.
   - Diagrama de despliegue.
   - Cambios respecto al prototipo 1.
   - Decisiones de diseño.
6. **Cambios y ajustes respecto al prototipo 1**
   - Qué se mejoró.
   - Qué se corrigió.
   - Qué se eliminó o reemplazó.
   - Impacto en API, protocolo, configuración, estructuras y rendimiento.
7. **Mejoras implementadas**
   - Memoria y paginación.
   - Red y protocolo.
   - MSI y coherencia.
   - Concurrencia y sincronización.
   - Manejo de errores y timeouts.
   - CLI y web.
8. **Aplicaciones de prueba**
   - Matrices.
   - Archivo/video.
   - Incrementos concurrentes.
   - Adaptación al API.
   - Datos usados.
9. **Pruebas de estrés**
   - Escenarios.
   - Configuración.
   - Resultados.
   - Problemas.
10. **Métricas y rendimiento**
    - Fallos de página.
    - Lecturas/escrituras remotas.
    - Invalidaciones.
    - Bytes transferidos.
    - Latencia.
    - Throughput.
    - Speedup.
    - Uso de memoria por nodo.
11. **Limitaciones conocidas**
12. **Trabajo pendiente para la entrega final**
13. **Conclusiones**
14. **Referencias**
15. **Anexos**
    - Logs.
    - Capturas.
    - Tablas.
    - Gráficos.

---

## 5. Qué debe demostrar el prototipo 2

Mínimo:

- Compila y ejecuta en Linux.
- Arranca **4 nodos ideales** (mínimo 2).
- Varios hilos por nodo.
- Varias páginas.
- Transferencia remota.
- Invalidación funcional.
- MSI correcto en lectura/escritura.
- Timeouts y reintentos.
- CLI de consulta.
- Web básica.
- Al menos una aplicación real usando el API.
- Evidencia de datos reales.

Deseable:

- Dos aplicaciones reales.
- Pruebas de estrés.
- Métricas comparativas.
- Manejo de nodo caído.
- Extra 1 en progreso.
- Extra 2 en progreso.

---

## 6. Evidencia que debe incluir

- `make` exitoso.
- Arranque de 4 nodos.
- Logs de heartbeat.
- Logs de `PAGE_REQUEST`, `PAGE_DATA`, `INVALIDATE`.
- Prueba de coherencia.
- Prueba de estrés.
- Resultados de la aplicación de matrices.
- Resultados de la aplicación de archivo/video.
- Checksums o verificación de resultados.
- Capturas de CLI.
- Capturas de web.
- `git log` con commits de ambos.
- Enlace al repositorio.

---

## 7. Checklist de entrega

- [ ] Documento Markdown del prototipo 2.
- [ ] Arquitectura actualizada.
- [ ] Cambios respecto al prototipo 1.
- [ ] Código fuente.
- [ ] `Makefile`.
- [ ] `README`.
- [ ] Configuración.
- [ ] Aplicaciones de prueba.
- [ ] Pruebas de estrés.
- [ ] Métricas.
- [ ] Evidencia.
- [ ] Limitaciones.
- [ ] Trabajo pendiente.
- [ ] Repositorio Git.
- [ ] Commits de ambos integrantes.

---

## 8. Criterios de aceptación

- El prototipo 2 mejora de forma medible al prototipo 1.
- La coherencia MSI funciona en escenarios concurrentes.
- Las aplicaciones usan el API DSM.
- Los resultados son verificables.
- Las métricas están documentadas.
- La documentación explica qué cambió y por qué.
- El repositorio tiene commits de ambos integrantes.
- Las instrucciones permiten reproducir las pruebas.

---

## 9. Riesgos típicos de esta etapa

| Riesgo | Mitigación |
|---|---|
| Mezclar mejoras sin medir | Medir antes/después de cada cambio. |
| Aplicaciones no adaptadas | Reemplazar `malloc` por `dsm_malloc` y validar. |
| False sharing | Alinear datos y separar contadores. |
| Deadlocks bajo carga | Timeouts, locks por página, pruebas de estrés. |
| Web distrae del núcleo | Web básica y de solo lectura. |
| Extras consumen tiempo | Base primero; extras después del E3. |
| Git con un solo autor | Commits frecuentes de ambos. |

---

## 10. Próximo paso

Implementar el prototipo 2, ejecutar las pruebas de estrés y redactar `Entregable_3/03_Entregable_3_Prototipo_2.md` con la arquitectura actualizada, los cambios y las métricas.
