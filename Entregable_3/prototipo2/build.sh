#!/bin/sh
# Compila dsm_node y dsmctl nativamente en Linux Mint/glibc.
# Forzamos un header común que trae todo lo que glibc necesita.

set -e

CFLAGS="-std=c11 -D_GNU_SOURCE -Iinclude -O2 -g -include common_glibc.h"

# Crear header temporal con includes que faltan en glibc
cat > common_glibc.h << 'EOF'
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
EOF

gcc $CFLAGS -o dsm_node \
    src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c \
    src/config.c src/protocol.c src/log.c src/nodes.c src/net.c \
    src/dsm_api.c src/runtime.c src/demo_page.c src/apps.c \
    src/app_common.c src/app_matrices.c src/app_file.c src/app_counter.c \
    src/app_bench.c src/app_evict.c src/app_chat.c src/control.c src/web.c src/heartbeat.c \
    src/node_main.c -lpthread -lm -lrt

gcc $CFLAGS -o dsmctl \
    src/ctl_main.c src/config.c -lpthread -lm

rm -f common_glibc.h
ls -la dsm_node dsmctl
echo "OK: binarios listos"