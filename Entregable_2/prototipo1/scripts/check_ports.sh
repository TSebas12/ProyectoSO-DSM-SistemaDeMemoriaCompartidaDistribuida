#!/usr/bin/env bash
# Verifica que el nodo abre su puerto de escucha en modo servidor.
# Uso: sh scripts/check_ports.sh
set -e
cd "$(dirname "$0")/.."

mkdir -p logs evidence

./dsm_node --config config/dsm.conf --serve > logs/serve0.out 2>&1 &
pid=$!
sleep 1

{
    echo "# netstat -ltn (solo puertos 27300-27307)"
    netstat -ltn 2>/dev/null | grep -E '2730[0-7]' || echo "(sin coincidencias)"
} > evidence/04_ports.txt

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

cat evidence/04_ports.txt
