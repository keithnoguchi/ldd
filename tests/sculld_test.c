/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kselftest.h"

static int test_opendir(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL)
		return errno;
	closedir(dir);
	return 0;
}

static int test_not_opendir(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL)
		return 0;
	closedir(dir);
	return EEXIST;
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

static int test_no_open_file_read_only(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;
	close(fd);
	return ENODEV;
}

static int test_no_open_file_write_only(const char *path)
{
	int fd = open(path, O_WRONLY);
	if (fd == -1)
		return 0;
	close(fd);
	return ENODEV;
}

static int test_no_open_file_read_write(const char *path)
{
	int fd = open(path, O_RDWR);
	if (fd == -1)
		return 0;
	close(fd);
	return ENODEV;
}

static int test_size(const char *path, size_t want)
{
	char buf[BUFSIZ];
	int err = 0;
	int ret;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		err = errno;
		goto out;
	}
	ret = read(fd, buf, sizeof(buf));
	if (ret == -1) {
		err = errno;
		goto out;
	}
	ret = strtol(buf, NULL, 10);
	if (ret == -1) {
		err = errno;
		goto out;
	}
	if (ret != want) {
		err = EINVAL;
		goto out;
	}
out:
	if (fd != -1)
		close(fd);
	return err;
}

static int test_readn(const char *path, size_t len, int n)
{
	char *buf = NULL;
	int err = 0;
	int pos;
	int fd;
	int i;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		err = errno;
		goto out;
	}

	buf = malloc(len);
	if (!buf) {
		int err = errno;
		goto out;
	}

	for (i = 0; i < n; i++) {
		pos = 0;
		while (pos < len) {
			ssize_t ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				int err = errno;
				goto out;
			}
			printf("%ld = read(%s:%d)\n", ret, path, len-pos);
			pos += ret;
		}
	}
out:
	if (buf)
		free(buf);
	if (fd != -1)
		close(fd);
	return err;
}

static int test_writen(const char *path, size_t len, int n)
{
	char *buf = NULL;
	int err = 0;
	int pos;
	int fd;
	int i;

	fd = open(path, O_WRONLY);
	if (fd == -1) {
		err = errno;
		goto out;
	}
	buf = malloc(len);
	if (!buf) {
		err = errno;
		goto out;
	}
	for (i = 0; i < n; i++) {
		pos = 0;
		while (pos < len) {
			ssize_t ret = write(fd, buf+pos, len-pos);
			if (ret == -1) {
				err = errno;
				goto out;
			}
			pos += ret;
		}
	}
out:
	if (buf)
		free(buf);
	if (fd != -1)
		close(fd);
	return err;
}

static int test_sculld_open(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		const int	(*func)(const char *path);
	} tests[] = {
		{
			.name	= "sculld0 device directory",
			.path	= "/sys/devices/sculld0",
			.func	= test_opendir,
		},
		{
			.name	= "sculld0 device uevent file",
			.path	= "/sys/devices/sculld0/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld0 device subsystem directory",
			.path	= "/sys/devices/sculld0/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "sculld0 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld0",
			.func	= test_opendir,
		},
		{
			.name	= "sculld0 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld0/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld1 device directory",
			.path	= "/sys/devices/sculld1",
			.func	= test_opendir,
		},
		{
			.name	= "sculld1 device uevent file",
			.path	= "/sys/devices/sculld1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld1 device subsystem directory",
			.path	= "/sys/devices/sculld1/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "sculld1 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld1",
			.func	= test_opendir,
		},
		{
			.name	= "sculld1 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld2:1 device directory",
			.path	= "/sys/devices/sculld2:1",
			.func	= test_opendir,
		},
		{
			.name	= "sculld2:1 device uevent file",
			.path	= "/sys/devices/sculld2:1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld2:1 device subsystem directory",
			.path	= "/sys/devices/sculld2:1/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "sculld2:1 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld2:1",
			.func	= test_opendir,
		},
		{
			.name	= "sculld2:1 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculld2:1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculldX device directory",
			.path	= "/sys/devices/sculldX",
			.func	= test_opendir,
		},
		{
			.name	= "sculldX device uevent file",
			.path	= "/sys/devices/sculldX/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculldX device subsystem directory",
			.path	= "/sys/devices/sculldX/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "sculldX device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculldX",
			.func	= test_opendir,
		},
		{
			.name	= "sculldX device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/sculldX/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld driver directory",
			.path	= "/sys/bus/ldd/drivers/sculld",
			.func	= test_opendir,
		},
		{
			.name	= "sculld0 device under sculld driver",
			.path	= "/sys/bus/ldd/drivers/sculld/sculld0",
			.func	= test_opendir,
		},
		{
			.name	= "sculld1 device under sculld driver",
			.path	= "/sys/bus/ldd/drivers/sculld/sculld1",
			.func	= test_opendir,
		},
		{
			.name	= "sculld2:1 device under sculld driver",
			.path	= "/sys/bus/ldd/drivers/sculld/sculld2:1",
			.func	= test_opendir,
		},
		{
			.name	= "sculldX device should not be under sculld driver",
			.path	= "/sys/bus/ldd/drivers/sculld/sculldX",
			.func	= test_not_opendir,
		},
		{
			.name	= "sculld0 device under /dev directory",
			.path	= "/dev/sculld0",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld1 device under /dev directory",
			.path	= "/dev/sculld1",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculld2:1 device under /dev directory",
			.path	= "/dev/sculld2:1",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "sculldX device under /dev directory",
			.path	= "/dev/sculldX",
			.func	= test_no_open_file_read_write,
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

static int test_sculld_write(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
		const int	(*func)(const char *name, size_t len, int count);
	} tests[] = {
		{
			.name	= "Write 8KiB to /dev/sculld0",
			.dev	= "sculld0",
			.len	= 4096,
			.count	= 2,
			.func	= test_writen,
		},
		{
			.name	= "Write 8KiB to /dev/sculld1",
			.dev	= "sculld1",
			.len	= 4096,
			.count	= 2,
			.func	= test_writen,
		},
		{
			.name	= "Write 8KiB to /dev/sculld2:1",
			.dev	= "sculld2:1",
			.len	= 4096,
			.count	= 2,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/sculld0",
			.dev	= "sculld0",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/sculld1",
			.dev	= "sculld1",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/sculld2:1",
			.dev	= "sculld2:1",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 bytes to /dev/sculld0",
			.dev	= "sculld0",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 byte to /dev/sculld1",
			.dev	= "sculld1",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 byte to /dev/sculld2:1",
			.dev	= "sculld2:1",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/sculld0",
			.dev	= "sculld0",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/sculld1",
			.dev	= "sculld1",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/sculld2:1",
			.dev	= "sculld2:1",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 0 byte to /dev/sculld0",
			.dev	= "sculld0",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 0 byte to /dev/sculld1",
			.dev	= "sculld1",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 0 byte to /dev/sculld2:1",
			.dev	= "sculld2:1",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{},	/* sentry */
	};
	const struct test *t;
	ssize_t bufsiz = 0;
	int fail = 0;

	for (t = &tests[0]; t->name; t++) {
		char buf[BUFSIZ];
		int err;

		sprintf(buf, "/dev/%s", t->dev);
		err = (*t->func)(buf, t->len, t->count);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		/* current file size test */
		sprintf(buf, "/sys/devices/%s/size", t->dev);
		err = test_size(buf, t->len*t->count);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			fail++;
			continue;
		}
		/* bufsiz is the biggest write size ever */
		if (bufsiz < t->len*t->count)
			bufsiz = t->len*t->count;
		sprintf(buf, "/sys/devices/%s/bufsiz", t->dev);
		err = test_size(buf, bufsiz);
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

	fail = test_sculld_open();
	fail += test_sculld_write();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
