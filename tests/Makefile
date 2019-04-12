# SPDX-License-Identifier: GPL-2.0
KERNDIR ?= /lib/modules/$(shell uname -r)/build
TEST_GEN_PROGS := $(patsubst %.c,%,$(sort $(wildcard *_test.c)))
include $(KERNDIR)/tools/testing/selftests/lib.mk
