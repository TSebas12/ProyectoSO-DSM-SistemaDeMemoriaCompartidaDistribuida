# 21. Anexos

## 21.1 Logs

Logs por nodo en `logs/dsm-nodeN.log`. Incluyen fallos de página, invalidaciones, expulsiones, reenvíos y errores.

## 21.2 Capturas

Capturas de salida de cada aplicación (A y B) y de `stats.json`, organizadas por escenario (S1–S11).

## 21.3 Tablas

- Tabla de nodos (`config/nodes_N.conf`).
- Tabla de páginas locales (`dsmctl pages`).
- Directorio de páginas (`dsmctl dir`).

## 21.4 Gráficos

[MEDIR] Latencias por nodo, speedup vs N nodos, uso de memoria pico.

## 21.5 Evidencia de Git

```bash
git log --oneline --all --graph
git shortlog -sn
```

Verificar que ambos integrantes tengan commits propios.

## 21.6 Configuración de ejemplo

Ejemplo para 2 nodos en loopback (`config/nodes_2.conf`):

```
0 127.0.0.1 27300
1 127.0.0.1 27304
```

Huella de configuración: [MEDIR] (debe ser idéntica en todos los nodos).