/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "kselftest.h"

static int test_open(const char *path, int flags)
{
	int fd = open(path, flags);
	if (fd == -1)
		return errno;
	close(fd);
	return 0;
}

static int test_sleepy_open(void)
{
	const struct test {
		const char	*name;
		int		flags;
	} tests[] = {
		{
			.name	= "/dev/sleepy read only open",
			.flags	= O_RDONLY,
		},
		{
			.name	= "/dev/sleepy write only open",
			.flags	= O_WRONLY,
		},
		{
			.name	= "/dev/sleepy read-write open",
			.flags	= O_RDWR,
		},
		{},	/* sentry */
	};
	const char *path = "/dev/sleepy";
	const struct test *tc;
	int fail = 0;

	for (tc = tests; tc->name; tc++) {
		int err = test_open(path, tc->flags);
		if (err) {
			errno = err;
			perror(tc->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		ksft_inc_pass_cnt();
	}
	return fail;
}

int main(void)
{
	int fail;

	fail = test_sleepy_open();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
