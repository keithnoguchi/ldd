# SPDX-License-Idenfitier: GPL-2.0
MODS    := scull
MODS    += sleepy
MODS    += ldd
MODS    += sculld
obj-m   += $(patsubst %,%.o,$(MODS))
KERNDIR ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help:
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
clean: clean_tests
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
load:
	@for mod in $(MODS); do modprobe $$mod; done
unload:
	@-for mod in $(MODS); do rmmod $$mod; done
# selftest based unit tests under tests directory.
.PHONY: test run_tests clean_tests
test: unload modules modules_install load run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests \
		CFLAGS="-I$(KERNDIR)/tools/testing/selftests -I$(shell pwd)" $@
clean_tests:
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests clean
