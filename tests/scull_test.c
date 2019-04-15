/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
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

static int test_attr_getl(const char *path, long *get)
{
	char buf[BUFSIZ];
	int err = 0;
	long got;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return errno;

	err = read(fd, buf, sizeof(buf));
	if (err == -1) {
		err = errno;
		goto out;
	}

	got = strtol(buf, NULL, 10);
	if (got <= LONG_MIN || got >= LONG_MAX) {
		err = EINVAL;
		goto out;
	}
	err = 0;
	*get = got;
out:
	close(fd);
	return err;
}

static int test_attr_readl(const char *path, long want)
{
	long got;
	int err;

	err = test_attr_getl(path, &got);
	if (err)
		return err;
	if (got != want) {
		fprintf(stderr, "got(%ld)!=want(%ld)\n", got, want);
		return EINVAL;
	}
	return 0;
}

static ssize_t test_readn(const char *path, size_t len, int n, off_t offset,
			  size_t *got)
{
	char *buf = NULL;
	size_t total;
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
	if (offset)
		if (lseek(fd, offset, SEEK_SET) == -1) {
			err = errno;
			goto out;
		}
	total = 0;
	for (i = 0; i < n; i++) {
		int pos = 0;
		while (pos < len) {
			int ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				err = errno;
				goto out;
			}
			/* treat the zero read as success */
			if (ret == 0)
				break;
			pos += ret;
		}
		total += pos;
	}
	*got = total;
	err = 0;
out:
	if (buf)
		free(buf);
	close(fd);
	return err;
}

static int test_writen(const char *path, size_t len, int nr, off_t offset)
{
	char *buf = NULL;
	int err;
	int fd;
	int i;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return errno;

	buf = malloc(len);
	if (buf == NULL) {
		err = errno;
		goto out;
	}
	if (offset)
		if (lseek(fd, offset, SEEK_SET) == -1) {
			err = errno;
			goto out;
		}
	for (i = 0; i < nr; i++) {
		int pos = 0;
		while (pos < len) {
			int ret = write(fd, buf+pos, len-pos);
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

static int test_writen_and_readn(const char *path, size_t len, int nr)
{
	char *wbuf = NULL, *rbuf = NULL;
	int wlen, rlen;
	int ret;
	int fd;
	int i;

	/* Reset the device by opening it write only */
	ret = test_open(path, O_WRONLY);
	if (ret)
		return ret;

	/* prepare the buffer */
	wbuf = malloc(len*nr);
	if (wbuf == NULL)
		return errno;
	memset(wbuf, 0xaa, len*nr);

	rbuf = malloc(len*nr);
	if (rbuf == NULL) {
		ret = errno;
		free(wbuf);
		return ret;
	}
	memset(rbuf, 0xbb, len*nr);

	/* Open file read and write */
	fd = open(path, O_RDWR);
	if (fd == -1)
		goto out;

	/* first write it. */
	wlen = 0;
	for (i = 0; i < nr; i++) {
		char *buf = &wbuf[len*i];
		int pos = 0;
		while (pos < len) {
			int ret = write(fd, buf+pos, len-pos);
			if (ret == -1) {
				ret = errno;
				goto out;
			}
			wlen += ret;
			pos += ret;
		}
	}

	/* Read file from the beginning */
	ret = lseek(fd, 0, SEEK_SET);
	if (ret == -1) {
		ret = errno;
		goto out;
	}

	rlen = 0;
	for (i = 0; i < nr; i++) {
		char *buf = &rbuf[len*i];
		int pos = 0;
		while (pos < len) {
			ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				ret = errno;
				goto out;
			}
			if (ret == 0)
				break;
			rlen += ret;
			pos += ret;
		}
	}
	if (rlen != wlen) {
		ret = EINVAL;
		goto out;
	}
	if (rlen != len*nr) {
		ret = EINVAL;
		goto out;
	}
	if (memcmp(rbuf, wbuf, len*nr)) {
		for (i = 0; i < len*nr; i++)
			if (rbuf[i] != wbuf[i]) {
				fprintf(stderr, "rbuf[%d](0x%hhx) != wbuf[%d](0x%hhx)\n",
					i, rbuf[i], i, wbuf[i]);
				break;
			}
		ret = EINVAL;
		goto out;
	}
	ret = 0;
out:
	if (rbuf)
		free(rbuf);
	if (wbuf)
		free(wbuf);
	if (fd != -1)
		close(fd);
	return ret;
}

static int test_scull_open(void)
{
	const struct test {
		const char	*const name;
		const char	*const path;
		int		flags;
	} *t, tests[] = {
		{
			.name	= "scull0 read only open",
			.path	= "/dev/scull0",
			.flags	= O_RDONLY,
		},
		{
			.name	= "scull0 write only open",
			.path	= "/dev/scull0",
			.flags	= O_WRONLY,
		},
		{
			.name	= "scull0 read/write open",
			.path	= "/dev/scull0",
			.flags	= O_RDWR,
		},
		{}, /* sentory */
	};
	int fail = 0;

	for (t = tests; t->name; t++) {
		int err = test_open(t->path, t->flags);
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

static int test_scull_attr_readl(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		long		want;
	} tests[] = {
		{
			.name	= "read scull0's pagesize",
			.path	= "/sys/devices/scull0/pagesize",
			.want	= 4096,
		},
		{
			.name	= "read scull0's quantum set size",
			.path	= "/sys/devices/scull0/quantum_set",
			.want	= 1024,
		},
		{
			.name	= "read scull0's quantum size",
			.path	= "/sys/devices/scull0/quantum",
			.want	= 4096,
		},
		{
			.name	= "read scull0's size",
			.path	= "/sys/devices/scull0/size",
			.want	= 0,
		},
		{
			.name	= "read scull0's buffer size",
			.path	= "/sys/devices/scull0/buffer/size",
			.want	= 0,
		},
		{},	/* sentry */
	};
	const struct test *t;
	int fail = 0;

	for (t = tests; t->name; t++) {
		int err = test_attr_readl(t->path, t->want);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		ksft_inc_pass_cnt();
	}
	return fail;
}

static int test_scull_readn(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
		off_t		offset;
	} tests[] = {
		{
			.name	= "read 0 byte from scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 0,
			.offset	= 0,
		},
		{
			.name	= "read 1 byte from scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "read 4096 bytes from scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "read 16KiB(4KiB x 4) from scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 4,
			.offset	= 0,
		},
		{
			.name	= "read 1 byte on 4KiB offset from scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 4096,
		},
		{}, /* sentry */
	};
	const struct test *t;
	char path[BUFSIZ];
	int fail = 0;
	size_t total;

	for (t = tests; t->name; t++) {
		int err = sprintf(path, "/dev/%s", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		total = 0;
		err = test_readn(path, t->len, t->count, t->offset, &total);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/size", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = test_attr_readl(path, total);
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

static int test_scull_writen(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
		off_t		offset;
	} tests[] = {
		{
			.name	= "write 0 byte on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1 byte on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 4KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 4097 bytes on scull0",
			.dev	= "scull0",
			.len	= 4097,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 8KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 2,
			.offset	= 0,
		},
		{
			.name	= "write 0 byte on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1 byte with 4KiB offset on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 4096,
		},
		{},	/* sentry */
	};
	const struct test *t;
	int fail = 0;

	for (t = tests; t->name; t++) {
		char path[BUFSIZ];
		long quantum, bufsize;
		int err;

		err = sprintf(path, "/dev/%s", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = test_writen(path, t->len, t->count, t->offset);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/size", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = test_attr_readl(path, t->len*t->count+t->offset);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/quantum", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = test_attr_getl(path, &quantum);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/buffer/size", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		bufsize = 0;
		if ((t->len*t->count) != 0) {
			bufsize = ((t->len*t->count)/quantum)*quantum;
			if ((t->len*t->count)%quantum)
				bufsize += quantum;
		}
		err = test_attr_readl(path, bufsize);
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

static int test_scull_writen_and_readn(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
	} tests[] = {
		{
			.name	= "write and read 0 bytes on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
		},
		{
			.name	= "write and read 1 bytes on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
		},
		{
			.name	= "write and read 1KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 1,
		},
		{
			.name	= "write and read 4KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
		},
		{
			.name	= "write and read 4KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 4,
		},
		{
			.name	= "write and read 4097 bytes on scull0",
			.dev	= "scull0",
			.len	= 4097,
			.count	= 1,
		},
		{
			.name	= "write and read 8KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 2,
		},
		{},	/* sentry */
	};
	const struct test *t;
	int fail = 0;

	for (t = tests; t->name; t++) {
		char path[BUFSIZ];
		int err;

		err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		err = test_writen_and_readn(path, t->len, t->count);
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
	fail += test_scull_attr_readl();
	fail += test_scull_readn();
	fail += test_scull_writen();
	fail += test_scull_writen_and_readn();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
