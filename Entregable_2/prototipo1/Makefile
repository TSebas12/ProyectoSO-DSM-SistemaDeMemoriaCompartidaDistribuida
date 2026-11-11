# ============================================================
# Makefile provisional - parte de Sebastian Aguero Brenes
# Proyecto IC-6600 - Sistema de Memoria Compartida Distribuida
#
# Solo compila los modulos de Sebastian, sin enlazar: todavia no
# existen runtime.c, dsm_api.c ni la capa de red. Cuando Jose Daniel
# agregue su parte, el Makefile definitivo va en la raiz del repo y
# este archivo se elimina.
#
# Uso:  make check      compila los 5 modulos a objetos
#       make clean      borra build/
# ============================================================

CC       ?= gcc
CFLAGS   ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -Iinclude

BUILD := build
SRC   := src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c
OBJ   := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
HDR   := $(wildcard include/*.h)

.PHONY: all check clean

all: check

check: $(OBJ)
	@echo "==> Los 5 modulos compilan sin errores."

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
