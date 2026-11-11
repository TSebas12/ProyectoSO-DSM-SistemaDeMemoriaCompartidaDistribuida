#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
./dsm_node --config config/dsm.conf "$@"