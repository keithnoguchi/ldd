# SPDX-License-Idenfitier: GPL-2.0
MODS  := open
MODS  += read
MODS  += write
MODS  += readv
MODS  += scull
MODS  += sleepy
# ldd bus based drivers
MODS  += ldd
MODS  += sculld
obj-m += $(patsubst %,%.o,$(MODS))
KDIR  ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help:
	$(MAKE) -C $(KDIR) M=$(shell pwd) $@
.PHONY: clean load unload reload
clean: clean_tests
	$(MAKE) -C $(KDIR) M=$(shell pwd) $@
load:
	@for mod in $(MODS); do insmod ./$${mod}.ko; done
unload:
	@# remove ldd.ko last
	@-for mod in $(filter-out ldd,$(MODS)); do rmmod ./$$mod.ko; done
	@-rmmod ./ldd.ko
reload: unload load
# selftest based unit tests under tests directory.
.PHONY: test run_tests clean_tests
test: modules reload run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests \
		CFLAGS=-I$(KDIR)/tools/testing/selftests $@
clean_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests clean
