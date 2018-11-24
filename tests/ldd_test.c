/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "kselftest.h"

static int test_opendir(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL)
		return errno;
	closedir(dir);
	return 0;
}

static int test_open_file_write_only(const char *path)
{
	int fd = open(path, O_WRONLY);
	if (fd == -1)
		return errno;
	close(fd);
	return 0;
}

static int test_ldd_open(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		const int	(*func)(const char *path);
	} tests[] = {
		{
			.name	= "ldd bus directory",
			.path	= "/sys/bus/ldd",
			.func	= test_opendir,
		},
		{
			.name	= "ldd bus uevent file",
			.path	= "/sys/bus/ldd/uevent",
			.func	= test_open_file_write_only,
		},
		{},	/* sentry */
	};
	const struct test *t;
	int fail = 0;
	int err;

	for (t = &tests[0]; t->name; t++) {
		err = (*t->func)(t->path);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
		} else
			ksft_inc_pass_cnt();
	}
	return fail;
}

int main(void)
{
	int fail;

	fail = test_ldd_open();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
