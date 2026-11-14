CC       ?= gcc
CFLAGS   ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -Iinclude
LDFLAGS  ?=
LDLIBS   ?= -lpthread -lm

BUILD := build

# Módulos del nodo. El orden no importa: el enlazador resuelve.
NODE_SRC := \
	src/sync.c \
	src/stats.c \
	src/memory.c \
	src/fault.c \
	src/coherence.c \
	src/config.c \
	src/protocol.c \
	src/log.c \
	src/nodes.c \
	src/net.c \
	src/dsm_api.c \
	src/runtime.c \
	src/demo_page.c \
	src/apps.c \
	src/app_common.c \
	src/app_matrices.c \
	src/app_file.c \
	src/app_counter.c \
	src/app_bench.c \
	src/app_evict.c \
	src/control.c \
	src/web.c \
	src/heartbeat.c

NODE_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(NODE_SRC))
HDR      := $(wildcard include/*.h)

.PHONY: all check clean

all: check dsm_node dsmctl

check: $(NODE_OBJ) $(BUILD)/node_main.o $(BUILD)/ctl_main.o
	@echo "==> Modulos compilan sin errores."

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

dsm_node: $(NODE_OBJ) $(BUILD)/node_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(NODE_OBJ) $(BUILD)/node_main.o $(LDLIBS)

# dsmctl es un cliente de texto: solo necesita leer la configuración.
dsmctl: $(BUILD)/ctl_main.o $(BUILD)/config.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD)/ctl_main.o $(BUILD)/config.o $(LDLIBS)

clean:
	@rm -rf $(BUILD) dsm_node dsmctl
