#!/usr/bin/env bash
# Ensambla Entrega_Final/04_Entrega_Final.md a partir de secciones/*.md.
# Uso: ./ensamblar.sh
set -euo pipefail
cd "$(dirname "$0")"

OUT="04_Entrega_Final.md"
: > "$OUT"

for f in secciones/*.md; do
    cat "$f" >> "$OUT"
    printf "\n\n" >> "$OUT"
done

echo "Generado: $OUT ($(wc -l < "$OUT") lineas)"
echo "Secciones incluidas:"
ls secciones/*.md | sed 's/^/  - /'
