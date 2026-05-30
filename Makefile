# SPDX-License-Identifier: MIT
#
# Makefile simples para compilar o overlay.
# O objeto intermediario fica em /tmp/crosshair-build e o binario final fica
# na raiz do projeto como ./crosshair.

CC		?= cc
BUILD_DIR	:= /tmp/crosshair-build
TARGET		:= crosshair
SRC		:= crosshair.c
OBJ		:= $(BUILD_DIR)/crosshair.o

CFLAGS		?= -Wall -Wextra -Werror -std=c11 -pedantic -O2
CPPFLAGS	?=
LDFLAGS		?=
LDLIBS		:= -lX11

.PHONY: all clean run

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(OBJ): $(SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
