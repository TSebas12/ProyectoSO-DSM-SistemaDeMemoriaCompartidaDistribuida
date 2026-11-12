#!/usr/bin/env bash
# Uso: ./scripts/run.sh [config/dsm.conf|config/dsm-node1.conf] [opciones]
set -e
cd "$(dirname "$0")/.."
cfg="${1:-config/dsm.conf}"
if [ "$#" -gt 0 ]; then shift; fi
exec ./dsm_node --config "$cfg" "$@"
