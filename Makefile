# SPDX-License-Idenfitier: GPL-2.0
obj-m += ldd.o
ldd-objs := main.o
KERNDIR ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help clean:
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
# kernel's selftest based unit test.
distclean: clean unload
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests clean
test: modules modules_install load run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests \
		CFLAGS="-I$(KERNDIR)/tools/testing/selftests -I$(shell pwd)" $@
load:
	modprobe ldd
unload:
	rmmod ldd
