#!/bin/sh
# Compila dsm_node y dsmctl nativamente en Linux Mint/glibc.
# Necesita _GNU_SOURCE para sem_timedwait, CLOCK_MONOTONIC, memfd_create, etc.

set -e

CFLAGS="-std=c11 -D_GNU_SOURCE -Iinclude -O2 -g"

gcc $CFLAGS -o dsm_node \
    src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c \
    src/config.c src/protocol.c src/log.c src/nodes.c src/net.c \
    src/dsm_api.c src/runtime.c src/demo_page.c src/apps.c \
    src/app_common.c src/app_matrices.c src/app_file.c src/app_counter.c \
    src/app_bench.c src/app_evict.c src/control.c src/web.c src/heartbeat.c \
    src/node_main.c -lpthread -lm -lrt

gcc $CFLAGS -o dsmctl \
    src/ctl_main.c src/config.c -lpthread -lm

ls -la dsm_node dsmctl
echo "OK: binarios listos"