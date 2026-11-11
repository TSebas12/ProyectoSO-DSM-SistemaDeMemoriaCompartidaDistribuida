CC       ?= gcc
CFLAGS   ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -Iinclude
LDFLAGS  ?=
LDLIBS   ?= -lpthread

BUILD := build
SRC := src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c \
       src/config.c src/protocol.c src/log.c src/net.c src/dsm_api.c \
       src/runtime.c src/demo_page.c
OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
HDR := $(wildcard include/*.h)

.PHONY: all check clean

all: check dsm_node dsmctl

check: $(OBJ)
	@echo "==> Modulos compilan sin errores."

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

dsm_node: $(OBJ) $(BUILD)/node_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(BUILD)/node_main.o $(LDLIBS)

dsmctl: $(OBJ) $(BUILD)/ctl_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(BUILD)/ctl_main.o $(LDLIBS)

$(BUILD)/node_main.o: src/node_main.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/ctl_main.o: src/ctl_main.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD) dsm_node dsmctl