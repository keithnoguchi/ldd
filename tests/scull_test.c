/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
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

static int test_readn(const char *path, size_t len, int n)
{
	char *buf = NULL;
	int err;
	int fd;
	int i;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return errno;

	buf = malloc(len);
	if (buf == NULL) {
		err = errno;
		goto out;
	}

	for (i = 0; i < n; i++) {
		int pos = 0;
		while (pos < len) {
			int ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				err = errno;
				goto out;
			}
			pos += ret;
		}
	}
	err = 0;
out:
	if (buf)
		free(buf);
	close(fd);
	return err;
}

static int test_scull_open(void)
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

static int test_scull_read(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		size_t		len;
		int		count;
	} tests[] = {
		{
			.name	= "read 0 byte from scull0",
			.path	= "/dev/scull0",
			.len	= 0,
			.count	= 0,
		},
		{
			.name	= "read 1 byte from scull0",
			.path	= "/dev/scull0",
			.len	= 1,
			.count	= 1,
		},
		{
			.name	= "read 4096 bytes from scull0",
			.path	= "/dev/scull0",
			.len	= 4096,
			.count	= 1,
		},
		{
			.name	= "read 16KiB(4KiB x 4) from scull0",
			.path	= "/dev/scull0",
			.len	= 4096,
			.count	= 4,
		},
		{}, /* sentry */
	};
	const struct test *t;
	int fail = 0;

	for (t = tests; t->name; t++) {
		int err = test_readn(t->path, t->len, t->count);
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

	fail = test_scull_open();
	fail += test_scull_read();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
