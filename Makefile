# SPDX-License-Idenfitier: GPL-2.0
MODS  := scull
MODS  += sleepy
obj-m += $(patsubst %,%.o,$(MODS))
#ldd-objs += sculld.o
KERNDIR ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help:
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
# kernel's selftest based unit test.
clean: unload
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests $@
test: unload modules modules_install load run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests \
		CFLAGS="-I$(KERNDIR)/tools/testing/selftests -I$(shell pwd)" $@
load:
	@for mod in $(MODS); do modprobe $$mod; done
unload:
	@-for mod in $(MODS); do rmmod $$mod; done
