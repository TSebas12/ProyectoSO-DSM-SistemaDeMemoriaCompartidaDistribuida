# Entrega Final — Demo en Vivo en el Laboratorio

| Campo | Valor |
|---|---|
| **Curso** | IC-6600 Principios de Sistemas Operativos |
| **Proyecto** | Sistema de Memoria Compartida Distribuida (DSM) con Paginación bajo Demanda y Coherencia MSI |
| **Integrantes** | José Daniel Monterrosa Quirós · Sebastián Aguero Brenes |
| **Fecha oficial** | Viernes 27 de noviembre de 2026 |
| **Documento a generar** | `Entrega_Final/04_Entrega_Final.md` |
| **Documentos base** | General, E1, E2 y E3 |
| **Estado** | Pendiente |
| **Formato** | Markdown académico formal; exportable a Word/PDF si se solicita |

---

## 1. Objetivo

Entregar el sistema DSM completo, documentado y demostrable, y realizar la **demo en vivo** en los laboratorios de la Escuela de Ingeniería en Computación, Cartago, Lab H o similar, con defensa individual.

---

## 2. Qué pide el PDF

> **Entrega Final:** Demo en Vivo en el Laboratorio mediante citas de revisión.

Además, el enunciado exige:

- Código fuente documentado y modular.
- `Makefile`.
- Guía de configuración, instalación y ejecución.
- Enlace al repositorio Git.
- Commits de cada persona autora durante las semanas de trabajo.
- Aplicaciones cliente reales usando el espacio DSM.
- Prueba de estrés / validación con datos reales y salidas esperadas.
- Informe técnico completo.
- Manual de usuario.
- Demo en vivo.
- Defensa individual.

---

## 3. Entregables concretos

- Código fuente final.
- `Makefile`.
- `README` completo.
- Configuración de ejemplo.
- Aplicaciones de prueba.
- Informe técnico final.
- Manual de usuario.
- Repositorio Git.
- Evidencia de pruebas.
- Demo en vivo.

---

## 4. Estructura obligatoria del informe final

1. **Portada**
2. **Resumen ejecutivo**
3. **Índice**
4. **Introducción**
   - Problema.
   - Objetivos.
   - Alcance.
5. **Marco teórico**
   - DSM.
   - Paginación bajo demanda.
   - MSI.
   - Señales.
   - Sockets.
   - Pthreads y semáforos.
6. **Arquitectura final**
   - Diagrama de componentes.
   - Diagrama de despliegue.
   - Modelo de memoria.
   - Administración distribuida.
   - Heartbeat.
7. **Módulos implementados**
   - Memoria virtual.
   - Red y protocolo.
   - Concurrencia y sincronización.
   - MSI.
   - CLI.
   - Web.
8. **Diagrama de estados MSI**
9. **Concurrencia entre señales y sockets**
   - `SIGSEGV`.
   - `sigaction`.
   - `fault_resolver`.
   - Hilo de red.
   - Locks y semáforos.
10. **Interbloqueos y su resolución**
    - Grafo de espera.
    - Timeouts.
    - Orden de locks.
    - Reintentos.
    - Pruebas.
11. **Estructuras de datos y archivos**
    - Tablas.
    - Directorio.
    - Configuración.
    - Logs.
12. **API implementado**
    - Tipos.
    - Funciones.
    - Parámetros.
    - Entradas.
    - Salidas.
    - Errores.
13. **Protocolo de mensajería**
    - Cabecera.
    - Mensajes.
    - Payloads.
    - Flujos.
    - Errores.
14. **Aplicaciones de prueba**
    - Matrices.
    - Archivo/video.
    - Incrementos.
    - Datos reales.
    - Resultados esperados.
15. **Pruebas de uso y ejecución**
    - Escenarios.
    - Resultados.
    - Métricas.
    - Problemas.
16. **Rendimiento**
    - Latencia.
    - Throughput.
    - Speedup.
    - Uso de memoria.
17. **Extras**
    - Ejecución distribuida de procesos.
    - Tolerancia a fallos y redundancia.
    - Nivel de tolerancia.
    - Overhead.
18. **Manual de usuario**
    - Requisitos.
    - Compilación.
    - Configuración.
    - Arranque.
    - Comandos CLI.
    - Web.
    - Solución de problemas.
19. **Conclusiones**
20. **Referencias**
21. **Anexos**
    - Logs.
    - Capturas.
    - Tablas.
    - Gráficos.
    - Evidencia de Git.

---

## 5. Demo en vivo

### Condiciones del PDF

- Laboratorios de la Escuela de Ing. en Computación, Cartago.
- Lab H o similar.
- Se reservan de 1 a 2 horas por pareja.
- Ambas personas deben estar presentes.
- Ambas deben poder explicar todas y cada una de las partes.
- La nota es individual.
- Se evalúa cumplimiento de la especificación y defensa.

### Guion sugerido

1. **0–5 min:** contexto, arquitectura y objetivos.
2. **5–15 min:** arranque del clúster y configuración.
3. **15–30 min:** CLI: nodos, heartbeat, estadísticas, tablas de páginas.
4. **30–50 min:** aplicación de matrices con 4 nodos ideales.
5. **50–70 min:** aplicación de archivo/video y verificación de checksum.
6. **70–85 min:** prueba de estrés y coherencia.
7. **85–95 min:** fallo de nodo o extra de tolerancia, si se implementó.
8. **95–110 min:** web/consulta global y cierre ordenado.
9. **110–120 min:** preguntas y defensa.

### Preparación

- Repositorio clonado y compilado.
- Configuración de respaldo.
- Puertos del laboratorio probados.
- Datos de prueba listos.
- Logs o ejecución grabada de respaldo.
- Ensayo de la explicación de cada módulo.
- Respuestas preparadas sobre MSI, señales, deadlocks y rendimiento.

---

## 6. Checklist de entrega final

- [ ] Código fuente final.
- [ ] `Makefile`.
- [ ] `README`.
- [ ] Configuración de ejemplo.
- [ ] Aplicaciones de prueba.
- [ ] Informe técnico.
- [ ] Manual de usuario.
- [ ] Repositorio Git.
- [ ] Commits de ambos integrantes.
- [ ] Evidencia de pruebas.
- [ ] Métricas.
- [ ] Extras documentados.
- [ ] Demo ensayada.
- [ ] Ambos integrantes presentes.
- [ ] Defensa individual preparada.

---

## 7. Criterios de aceptación

- El sistema compila y ejecuta en el laboratorio.
- Se demuestra con al menos dos aplicaciones reales.
- Los datos se sincronizan entre nodos.
- Los resultados son verificables.
- La coherencia MSI funciona.
- La documentación explica todo el sistema.
- El manual permite reproducir la ejecución.
- El repositorio tiene commits de ambos.
- La demo se completa en el tiempo asignado.
- Ambos integrantes explican todas las partes.

---

## 8. Riesgos típicos de la demo

| Riesgo | Mitigación |
|---|---|
| La red del laboratorio falla | Configuración de respaldo y ejecución grabada. |
| Los puertos están ocupados | Rango alternativo y validación previa. |
| Un nodo no arranca | Scripts de arranque y logs. |
| La app tarda demasiado | Dataset ajustado y tiempos medidos. |
| Un integrante no puede explicar | Ensayo cruzado de todas las partes. |
| Los extras fallan | Desactivarlos por configuración y documentar. |
| Git no muestra autoría | Verificar commits antes de la demo. |

---

## 9. Próximo paso

Cerrar el sistema, ejecutar las pruebas finales, preparar el informe y el manual, y ensayar la demo en el laboratorio.
