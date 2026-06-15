# Makefile for GhostBlade SPI Bridge Linux Kernel Driver
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later

obj-m += apex_bridge.o

# Cross-compilation settings
# For native build on RK3576:
#   make
#
# For cross-compilation on x86 host:
#   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
#
# For out-of-tree build against specific kernel:
#   make -C /path/to/kernel/source M=$(PWD) modules

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean install uninstall

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f Module.symvers Module.markers modules.order

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/apex_bridge.ko
	depmod -a