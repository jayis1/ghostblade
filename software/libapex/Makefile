# libapex Makefile
# Copyright (C) 2026 Apex One Project
# SPDX-License-Identifier: GPL-2.0-or-later

CC      ?= gcc
AR      ?= ar
PREFIX  ?= /usr/local

CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -O2
CFLAGS  += -Iinclude
LDFLAGS ?=

SRC_DIR  = src
OBJ_DIR  = build
LIB_NAME = apex
LIB_STATIC = lib$(LIB_NAME).a
LIB_SHARED = lib$(LIB_NAME).so

SRCS = $(SRC_DIR)/libapex.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean install uninstall static shared

all: static shared

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

static: $(OBJ_DIR)/$(LIB_STATIC)

$(OBJ_DIR)/$(LIB_STATIC): $(OBJS)
	$(AR) rcs $@ $^

shared: $(OBJ_DIR)/$(LIB_SHARED)

$(OBJ_DIR)/$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS) -Wl,-soname,$(LIB_SHARED).0
	ln -sf $(LIB_SHARED) $(OBJ_DIR)/$(LIB_SHARED).0

install: all
	install -d $(PREFIX)/lib
	install -d $(PREFIX)/include
	install -m 644 $(OBJ_DIR)/$(LIB_STATIC) $(PREFIX)/lib/
	install -m 755 $(OBJ_DIR)/$(LIB_SHARED) $(PREFIX)/lib/
	install -m 644 include/libapex.h $(PREFIX)/include/
	ln -sf $(LIB_SHARED) $(PREFIX)/lib/$(LIB_SHARED).0
	ldconfig || true

uninstall:
	rm -f $(PREFIX)/lib/$(LIB_STATIC)
	rm -f $(PREFIX)/lib/$(LIB_SHARED)
	rm -f $(PREFIX)/lib/$(LIB_SHARED).0
	rm -f $(PREFIX)/include/libapex.h
	ldconfig || true

clean:
	rm -rf $(OBJ_DIR)