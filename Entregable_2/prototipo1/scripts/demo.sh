#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
make
./dsm_node --config config/dsm.conf --check-config