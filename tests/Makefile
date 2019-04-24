# SPDX-License-Identifier: GPL-2.0
KDIR           ?= /lib/modules/$(shell uname -r)/build
CFLAGS         += -Wall -O2 -I$(KDIR)/tools/testing/selftests
LDLIBS         += -lpthread
TEST_FILES     := $(sort $(wildcard *_test.c))
TESTS          ?= $(patsubst %.c,%,$(TEST_FILES))
TEST_GEN_PROGS := $(TESTS)
.PHONY: $(TESTS)
$(TESTS): $(TEST_FILES)
	@$(CC) $(CFLAGS) -o $@ $@.c $(LDLIBS)
	@./$@
include $(KDIR)/tools/testing/selftests/lib.mk
