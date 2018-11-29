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
		const char	*path;
		int		flags;
	} tests[] = {
		{
			.name	= "sleepy1 read only open",
			.path	= "/dev/sleepy1",
			.flags	= O_RDONLY,
		},
		{
			.name	= "sleepy1 write only open",
			.path	= "/dev/sleepy1",
			.flags	= O_WRONLY,
		},
		{
			.name	= "sleepy1 read-write open",
			.path	= "/dev/sleepy1",
			.flags	= O_RDWR,
		},
		{},	/* sentry */
	};
	const struct test *tc;
	int fail = 0;

	for (tc = tests; tc->name; tc++) {
		int err = test_open(tc->path, tc->flags);
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
