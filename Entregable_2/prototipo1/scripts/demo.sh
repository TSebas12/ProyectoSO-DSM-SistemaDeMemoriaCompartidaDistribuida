#!/usr/bin/env bash
# Levanta el nodo 0 (home) y el nodo 1, espera la demo y muestra la salida.
set -e
cd "$(dirname "$0")/.."

make
mkdir -p logs

./dsm_node --config config/dsm.conf > logs/demo-node0.out 2>&1 &
P0=$!
sleep 1
./dsm_node --config config/dsm-node1.conf > logs/demo-node1.out 2>&1 &
P1=$!

wait "$P1"
wait "$P0"

echo "===== nodo 0 (home) ====="
cat logs/demo-node0.out
echo "===== nodo 1 (cliente) ====="
cat logs/demo-node1.out
