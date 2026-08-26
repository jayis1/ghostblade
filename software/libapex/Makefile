# libapex Makefile
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later

CC      ?= gcc
AR      ?= ar
RANLIB  ?= ranlib
STRIP   ?= strip
PREFIX  ?= /usr/local
DESTDIR ?=

LIB_NAME  := apex
LIB_MAJOR := 0
SRC_DIR   := src
OBJ_DIR   := build

CPPFLAGS ?=
CPPFLAGS += -Iinclude
CFLAGS   ?= -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS  ?=
ARFLAGS  ?= rcsD

LIB_STATIC      := lib$(LIB_NAME).a
LIB_SHARED      := lib$(LIB_NAME).so
LIB_SHARED_SONAME := $(LIB_SHARED).$(LIB_MAJOR)
PKGCONFIG_FILE  := lib$(LIB_NAME).pc

SRCS = $(SRC_DIR)/libapex.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all clean install uninstall static shared pkgconfig

all: static shared pkgconfig

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

static: $(OBJ_DIR)/$(LIB_STATIC)

$(OBJ_DIR)/$(LIB_STATIC): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^
	$(RANLIB) $@

shared: $(OBJ_DIR)/$(LIB_SHARED) $(OBJ_DIR)/$(LIB_SHARED_SONAME)

$(OBJ_DIR)/$(LIB_SHARED): $(OBJS)
	$(CC) -shared -Wl,-soname,$(LIB_SHARED_SONAME) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/$(LIB_SHARED_SONAME): $(OBJ_DIR)/$(LIB_SHARED)
	ln -sfn $(LIB_SHARED) $@

pkgconfig: $(OBJ_DIR)/$(PKGCONFIG_FILE)

$(OBJ_DIR)/$(PKGCONFIG_FILE): libapex.pc.in | $(OBJ_DIR)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@LIBDIR@|$(PREFIX)/lib|g' \
		-e 's|@INCLUDEDIR@|$(PREFIX)/include|g' \
		$< > $@

install: all
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 644 $(OBJ_DIR)/$(LIB_STATIC) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(OBJ_DIR)/$(LIB_SHARED) $(DESTDIR)$(PREFIX)/lib/
	ln -sfn $(LIB_SHARED) $(DESTDIR)$(PREFIX)/lib/$(LIB_SHARED_SONAME)
	install -m 644 include/libapex.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(OBJ_DIR)/$(PKGCONFIG_FILE) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	@if [ -z "$(DESTDIR)" ]; then ldconfig || true; fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB_STATIC)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB_SHARED)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB_SHARED_SONAME)
	rm -f $(DESTDIR)$(PREFIX)/include/libapex.h
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/$(PKGCONFIG_FILE)
	@if [ -z "$(DESTDIR)" ]; then ldconfig || true; fi

clean:
	rm -rf $(OBJ_DIR)
