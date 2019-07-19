/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	const size_t	alloc;
	const char	*const data;
	const size_t	wsize;
	const size_t	seek;
	int		whence;
	const size_t	rsize;
	const size_t	size;
	const char	*const want;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[t->wsize > t->rsize ? t->wsize : t->wsize];
	int ret, fd;
	long val;
	FILE *fp;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/alloc", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != t->alloc) {
		fprintf(stderr, "%s: unexpected alloc value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->alloc, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_RDWR|O_TRUNC);
	if (fd == -1)
		goto perr;
	ret = write(fd, t->data, t->wsize);
	if (ret == -1)
		goto perr;
	if (ret != t->wsize) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n\t-  got: %d\n",
			t->name, t->wsize, ret);
		goto err;
	}
	ret = lseek(fd, t->seek, t->whence);
	if (ret == -1)
		goto perr;
	ret = read(fd, buf, sizeof(buf));
	if (ret == -1)
		goto perr;
	if (ret != t->rsize) {
		fprintf(stderr, "%s: unexpected read length:\n\t- want: %ld\n\t-  got: %d\n",
			t->name, t->rsize, ret);
		goto err;
	}
	if (t->rsize && memcmp(buf, t->want, t->rsize)) {
		buf[t->rsize] = '\0';
		fprintf(stderr, "%s: unexpected data:\n\t- want: '%s'\n\t-  got: '%s'\n",
			t->name, buf, t->want);
		goto err;
	}
	if (close(fd) == -1)
		goto perr;
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != t->size) {
		fprintf(stderr, "%s: unexpected length:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->size, val);
		goto err;
	}
	exit(EXIT_SUCCESS);
perr:
	perror(t->name);
err:
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "0 byte SEEK_SET on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 16,
			.size	= 16,
			.want	= "0123456789012345",
		},
		{
			.name	= "8 bytes SEEK_SET on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= 8,
			.whence	= SEEK_SET,
			.rsize	= 8,
			.size	= 16,
			.want	= "89012345",
		},
		{
			.name	= "16 bytes SEEK_SET on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= 16,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 16,
		},
		{
			.name	= "0 byte SEEK_SET on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 8,
			.size	= 8,
			.want	= "01234567",
		},
		{
			.name	= "8 bytes SEEK_SET on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= 8,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 8,
		},
		{
			.name	= "16 bytes SEEK_SET on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= 16,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 16,
		},
		{
			.name	= "0 byte SEEK_SET on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 64,
			.size	= 64,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "32 bytes SEEK_SET on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 32,
			.whence	= SEEK_SET,
			.rsize	= 32,
			.size	= 64,
			.want	= "23456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "64 bytes SEEK_SET on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 64,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 64,
		},
		{
			.name	= "0 byte SEEK_SET on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 32,
			.size	= 32,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
		},
		{
			.name	= "32 bytes SEEK_SET on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= 32,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 32,
		},
		{
			.name	= "64 bytes SEEK_SET on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= 64,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 64,
		},
		{
			.name	= "0 byte SEEK_SET on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 128,
			.size	= 128,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "64 bytes SEEK_SET on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 64,
			.whence	= SEEK_SET,
			.rsize	= 64,
			.size	= 128,
			.want	= "456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "128 bytes SEEK_SET on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 128,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 128,
		},
		{
			.name	= "0 byte SEEK_SET on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 64,
			.size	= 64,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "64 bytes SEEK_SET on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 64,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 64,
		},
		{
			.name	= "128 bytes SEEK_SET on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 128,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 128,
		},
		{
			.name	= "0 byte SEEK_SET on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 256,
			.size	= 256,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
		},
		{
			.name	= "128 bytes SEEK_SET on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= 128,
			.whence	= SEEK_SET,
			.rsize	= 128,
			.size	= 256,
			.want	= "89"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
		},
		{
			.name	= "256 bytes SEEK_SET on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= 256,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 256,
		},
		{
			.name	= "0 byte SEEK_SET on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 0,
			.whence	= SEEK_SET,
			.rsize	= 128,
			.size	= 128,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "128 bytes SEEK_SET on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 128,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 128,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "256 bytes SEEK_SET on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 256,
			.whence	= SEEK_SET,
			.rsize	= 0,
			.size	= 256,
		},
		{
			.name	= "0 byte SEEK_CUR on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 16,
		},
		{
			.name	= "-8 bytes SEEK_CUR on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= -8,
			.whence	= SEEK_CUR,
			.rsize	= 8,
			.want	= "89012345",
			.size	= 16,
		},
		{
			.name	= "-16 bytes SEEK_CUR on 16 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789012345",
			.wsize	= 16,
			.seek	= -16,
			.whence	= SEEK_CUR,
			.rsize	= 16,
			.want	= "0123456789012345",
			.size	= 16,
		},
		{
			.name	= "0 byte SEEK_CUR on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 8,
		},
		{
			.name	= "-8 bytes SEEK_CUR on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= -8,
			.whence	= SEEK_CUR,
			.rsize	= 8,
			.want	= "01234567",
			.size	= 8,
		},
		{
			.name	= "8 bytes SEEK_CUR on 8 bytes /dev/lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "01234567",
			.wsize	= 8,
			.seek	= 8,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 16,
		},
		{
			.name	= "0 byte SEEK_CUR on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 64,
		},
		{
			.name	= "-32 bytes SEEK_CUR on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= -32,
			.whence	= SEEK_CUR,
			.rsize	= 32,
			.size	= 64,
			.want	= "23456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "-64 bytes SEEK_CUR on 64 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= -64,
			.whence	= SEEK_CUR,
			.rsize	= 64,
			.size	= 64,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "0 byte SEEK_CUR on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 32,
		},
		{
			.name	= "-32 bytes SEEK_CUR on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= -32,
			.whence	= SEEK_CUR,
			.rsize	= 32,
			.size	= 32,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
		},
		{
			.name	= "32 bytes SEEK_CUR on 32 bytes /dev/lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"01",
			.wsize	= 32,
			.seek	= 32,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 64,
			.want	= "",
		},
		{
			.name	= "0 byte SEEK_CUR on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 128,
		},
		{
			.name	= "-64 bytes SEEK_CUR on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= -64,
			.whence	= SEEK_CUR,
			.rsize	= 64,
			.size	= 128,
			.want	= "456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "-128 bytes SEEK_CUR on 128 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= -128,
			.whence	= SEEK_CUR,
			.rsize	= 128,
			.size	= 128,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "0 byte SEEK_CUR on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 64,
		},
		{
			.name	= "-64 bytes SEEK_CUR on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= -64,
			.whence	= SEEK_CUR,
			.rsize	= 64,
			.size	= 64,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
		},
		{
			.name	= "64 bytes SEEK_CUR on 64 bytes /dev/lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123",
			.wsize	= 64,
			.seek	= 64,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 128,
		},
		{
			.name	= "0 byte SEEK_CUR on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 256,
		},
		{
			.name	= "-128 bytes SEEK_CUR on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= -128,
			.whence	= SEEK_CUR,
			.rsize	= 128,
			.size	= 256,
			.want	= "89"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
		},
		{
			.name	= "-256 bytes SEEK_CUR on 256 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
			.wsize	= 256,
			.seek	= -256,
			.whence	= SEEK_CUR,
			.rsize	= 256,
			.size	= 256,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"012345",
		},
		{
			.name	= "0 byte SEEK_CUR on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 0,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 128,
		},
		{
			.name	= "-128 bytes SEEK_CUR on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= -128,
			.whence	= SEEK_CUR,
			.rsize	= 128,
			.size	= 128,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{
			.name	= "128 bytes SEEK_CUR on 128 bytes /dev/lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
			.wsize	= 128,
			.seek	= 128,
			.whence	= SEEK_CUR,
			.rsize	= 0,
			.size	= 256,
			.want	= "0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"0123456789"
				"01234567",
		},
		{.name = NULL},
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1)
			goto perr;
		else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1)
			goto perr;
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: signaled with %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: does not exit\n",
				t->name);
			goto err;
		}
		if (WEXITSTATUS(status))
			goto err;
		ksft_inc_pass_cnt();
		continue;
perr:
		perror(t->name);
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
