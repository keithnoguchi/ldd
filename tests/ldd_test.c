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
			printf("%ld=write(%s:%d)\n", ret, path, len-pos);
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

static int test_open(void)
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
		{
			.name	= "scull0 device directory",
			.path	= "/sys/devices/scull0",
			.func	= test_opendir,
		},
		{
			.name	= "scull0 device uevent file",
			.path	= "/sys/devices/scull0/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull0 device subsystem directory",
			.path	= "/sys/devices/scull0/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "scull0 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull0",
			.func	= test_opendir,
		},
		{
			.name	= "scull0 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull0/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull1 device directory",
			.path	= "/sys/devices/scull1",
			.func	= test_opendir,
		},
		{
			.name	= "scull1 device uevent file",
			.path	= "/sys/devices/scull1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull1 device subsystem directory",
			.path	= "/sys/devices/scull1/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "scull1 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull1",
			.func	= test_opendir,
		},
		{
			.name	= "scull1 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull2:1 device directory",
			.path	= "/sys/devices/scull2:1",
			.func	= test_opendir,
		},
		{
			.name	= "scull2:1 device uevent file",
			.path	= "/sys/devices/scull2:1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull2:1 device subsystem directory",
			.path	= "/sys/devices/scull2:1/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "scull2:1 device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull2:1",
			.func	= test_opendir,
		},
		{
			.name	= "scull2:1 device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scull2:1/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scullX device directory",
			.path	= "/sys/devices/scullX",
			.func	= test_opendir,
		},
		{
			.name	= "scullX device uevent file",
			.path	= "/sys/devices/scullX/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scullX device subsystem directory",
			.path	= "/sys/devices/scullX/subsystem",
			.func	= test_opendir,
		},
		{
			.name	= "scullX device directory, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scullX",
			.func	= test_opendir,
		},
		{
			.name	= "scullX device uevent file, under ldd bus",
			.path	= "/sys/bus/ldd/devices/scullX/uevent",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull driver directory",
			.path	= "/sys/bus/ldd/drivers/scull",
			.func	= test_opendir,
		},
		{
			.name	= "scull0 device under scull driver",
			.path	= "/sys/bus/ldd/drivers/scull/scull0",
			.func	= test_opendir,
		},
		{
			.name	= "scull1 device under scull driver",
			.path	= "/sys/bus/ldd/drivers/scull/scull1",
			.func	= test_opendir,
		},
		{
			.name	= "scull2:1 device under scull driver",
			.path	= "/sys/bus/ldd/drivers/scull/scull2:1",
			.func	= test_opendir,
		},
		{
			.name	= "scullX device should not be under scull driver",
			.path	= "/sys/bus/ldd/drivers/scull/scullX",
			.func	= test_not_opendir,
		},
		{
			.name	= "scull0 device under /dev directory",
			.path	= "/dev/scull0",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull1 device under /dev directory",
			.path	= "/dev/scull1",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scull2:1 device under /dev directory",
			.path	= "/dev/scull2:1",
			.func	= test_open_file_read_write,
		},
		{
			.name	= "scullX device under /dev directory",
			.path	= "/dev/scullX",
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

static int test_io(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		size_t		len;
		int		count;
		const int	(*func)(const char *name, size_t len, int count);
	} tests[] = {
		{
			.name	= "Write 0 byte to /dev/scull0",
			.path	= "/dev/scull0",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 0 byte to /dev/scull1",
			.path	= "/dev/scull1",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 0 byte to /dev/scull2:1",
			.path	= "/dev/scull2:1",
			.len	= 0,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/scull0",
			.path	= "/dev/scull0",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/scull1",
			.path	= "/dev/scull1",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1 byte to /dev/scull2:1",
			.path	= "/dev/scull2:1",
			.len	= 1,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 bytes to /dev/scull0",
			.path	= "/dev/scull0",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 byte to /dev/scull1",
			.path	= "/dev/scull1",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 1024 byte to /dev/scull2:1",
			.path	= "/dev/scull2:1",
			.len	= 1024,
			.count	= 1,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/scull0",
			.path	= "/dev/scull0",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/scull1",
			.path	= "/dev/scull1",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{
			.name	= "Write 4KiB to /dev/scull2:1",
			.path	= "/dev/scull2:1",
			.len	= 1024,
			.count	= 4,
			.func	= test_writen,
		},
		{},	/* sentry */
	};
	const struct test *t;
	int fail = 0;

	for (t = &tests[0]; t->name; t++) {
		int err = (*t->func)(t->path, t->len, t->count);
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

	fail = test_open();
	fail += test_io();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
