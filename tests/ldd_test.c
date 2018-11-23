/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "kselftest.h"

static int test_opendir(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL)
		return errno;
	closedir(dir);
	return 0;
}

static int test_open_file_read_only(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd == -1)
		return errno;
	close(fd);
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

static int test_open_file_read_write(const char *path)
{
	int fd = open(path, O_RDWR);
	if (fd == -1)
		return errno;
	close(fd);
	return 0;
}

int main(void)
{
	struct test {
		const char	*name;
		const char	*path;
		int		(*func)(const char *path);
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
		{
			.name	= "scull0 device directory",
			.path	= "/sys/bus/ldd/devices/scull0",
			.func	= test_opendir,
		},
		{
			.name	= "scull0 device uevent file",
			.path	= "/sys/bus/ldd/devices/scull0/uevent",
			.func	= test_open_file_read_write,
		},
		{},	/* sentry */
	};
	struct test *t;
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
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
