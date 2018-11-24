/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "kselftest.h"

static int test_open(const char *path, mode_t mode)
{
	int fd = open(path, mode);
	if (fd == -1)
		return errno;
	close(fd);
	return 0;
}

static int test_scull_dev_open(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		mode_t		mode;
	} tests[] = {
		{
			.name	= "scull0 read only open",
			.path	= "/dev/scull0",
			.mode	= O_RDONLY,
		},
		{
			.name	= "scull0 write only open",
			.path	= "/dev/scull0",
			.mode	= O_WRONLY,
		},
		{
			.name	= "scull0 read/write open",
			.path	= "/dev/scull0",
			.mode	= O_RDWR,
		},
		{}, /* sentory */
	};
	const struct test *t;
	int fail = 0;

	for (t = tests; t->name; t++) {
		int err = test_open(t->path, t->mode);
		if (err) {
			errno = err;
			perror(t->name);
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

	fail = test_scull_dev_open();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
