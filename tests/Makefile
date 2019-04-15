# SPDX-License-Identifier: GPL-2.0
KDIR           ?= /lib/modules/$(shell uname -r)/build
CFLAGS         += -Wall -O2 -I$(KDIR)/tools/testing/selftests
LDLIBS         += -lpthread
TEST_GEN_PROGS := $(patsubst %.c,%,$(sort $(wildcard *_test.c)))
include $(KDIR)/tools/testing/selftests/lib.mk
