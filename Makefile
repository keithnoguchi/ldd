# SPDX-License-Idenfitier: GPL-2.0
MODS  := open
MODS  += read
MODS  += scull
MODS  += sleepy
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
	@for mod in $(MODS); do insmod $${mod}.ko; done
unload:
	-@for mod in $(MODS); do modprobe -r $$mod; done
reload: unload load
# selftest based unit tests under tests directory.
.PHONY: test run_tests clean_tests
test: modules_install load run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests \
		CFLAGS="-I$(KDIR)/tools/testing/selftests -I$(shell pwd)" $@
clean_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests clean
