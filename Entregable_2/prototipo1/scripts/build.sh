#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
make clean
make
echo "build OK"