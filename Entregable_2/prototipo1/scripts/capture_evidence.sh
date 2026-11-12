#!/usr/bin/env bash
# Regenera evidence/ con la salida real de compilación y de la demo.
# Uso: sh scripts/capture_evidence.sh
set -e
cd "$(dirname "$0")/.."

mkdir -p evidence logs

make clean > evidence/01_make.log 2>&1
make >> evidence/01_make.log 2>&1

./dsm_node --config config/dsm.conf --check-config > evidence/02_config.log 2>&1
./dsm_node --config config/dsm-node1.conf --check-config >> evidence/02_config.log 2>&1

sh scripts/demo.sh > evidence/03_demo.log 2>&1

echo "Evidencia generada en evidence/"
ls -1 evidence
