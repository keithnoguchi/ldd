/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	int		err;
	int		concurrent;
	struct iovec	iov[6];
	int		iovcount;
	size_t		total;
	size_t		want;
};

static void *tester(void *arg)
{
	const struct test *const t = arg;
	char buf[LINE_MAX];
	int ret, fd = -1;
	ssize_t got;
	FILE *fp;

	ret = snprintf(buf, sizeof(buf), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto err;
	fp = fopen(buf, "w");
	if (fp == NULL)
		goto err;
	ret = snprintf(buf, sizeof(buf), "%ld\n", t->total);
	if (ret < 0)
		goto err;
	if (fputs(buf, fp) == EOF)
		goto err;
	ret = fclose(fp);
	if (ret == -1)
		goto err;
	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, t->flags);
	if (fd == -1)
		goto err;
	if (!t->iovcount)
		pthread_exit(NULL);
	got = readv(fd, t->iov, t->iovcount);
	if (got == -1)
		goto err;
	if (got != t->want) {
		fprintf(stderr, "%s: unexpected readv result:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->want, got);
		pthread_exit((void *)EXIT_FAILURE);
	}
	pthread_exit(NULL);
err:
	if (t->err && errno == t->err)
		pthread_exit(NULL);
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	pthread_exit((void *)EXIT_FAILURE);
}

static void test(const struct test *restrict t)
{
	pthread_t tid[t->concurrent];
	void *retp;
	int i, ret;

	for (i = 0; i < t->concurrent; i++) {
		ret = pthread_create(&tid[i], NULL, tester, (void *)t);
		if (ret)
			goto err;
	}
	for (i = 0; i < t->concurrent; i++) {
		if (tid[i] == -1)
			continue;
		ret = pthread_join(tid[i], &retp);
		if (ret)
			goto err;
		if (retp != NULL)
			exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
err:
	fprintf(stderr, "%s: %s\n", t->name, strerror(ret));
	exit(EXIT_FAILURE);
}

int main(void)
{
	char buf[2][8192];
	const struct test *t, tests[] = {
		{
			.name		= "read only open",
			.dev		= "readv0",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iovcount	= 0,
		},
		{
			.name		= "write only open",
			.dev		= "readv0",
			.flags		= O_WRONLY,
			.err		= EINVAL,
			.concurrent	= 1,
			.iovcount	= 0,
		},
		{
			.name		= "read write open",
			.dev		= "readv0",
			.flags		= O_RDWR,
			.concurrent	= 1,
			.iovcount	= 0,
		},
		{
			.name		= "1 vector with 1 byte size",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],1}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 1,
		},
		{
			.name		= "1 vector with 32 bytes size",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],32}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 32,
		},
		{
			.name		= "1 vector with 64 bytes size",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],64}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "1 vector with 4096 bytes size",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],4096}},
			.iovcount	= 1,
			.total		= 4096,
			.want		= 4096,

		},
		{
			.name		= "1 vector with 8192 bytes size",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],8192}},
			.iovcount	= 1,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 1 byte size each",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],1},{buf[1],1}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 2,
		},
		{
			.name		= "2 vectors with 32 bytes size each",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],32},{buf[1],32}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "2 vectors with 64 bytes size each",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],64},{buf[1],64}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 128,
		},
		{
			.name		= "2 vectors with 4096 bytes size each",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],4096},{buf[1],4096}},
			.iovcount	= 2,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 8192 bytes size each",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],8192},{buf[1],8192}},
			.iovcount	= 2,
			.total		= 16384,
			.want		= 16384,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 16384,
			.want		= 12289,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each, with totla 10KB",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 1,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 10240,
			.want		= 10240,
		},
		{
			.name		= "1 vector with 1 byte size, concurrently",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],1}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 1,
		},
		{
			.name		= "1 vector with 32 bytes size, concurrently",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],32}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 32,
		},
		{
			.name		= "1 vector with 64 bytes size, concurrently",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],64}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "1 vector with 4096 bytes size, concurrently",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],4096}},
			.iovcount	= 1,
			.total		= 4096,
			.want		= 4096,

		},
		{
			.name		= "1 vector with 8192 bytes size, concurrently",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],8192}},
			.iovcount	= 1,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 1 byte size each, concurrently",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],1},{buf[1],1}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 2,
		},
		{
			.name		= "2 vectors with 32 bytes size each, concurrently",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],32},{buf[1],32}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "2 vectors with 64 bytes size each, concurrently",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],64},{buf[1],64}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 128,
		},
		{
			.name		= "2 vectors with 4096 bytes size each, concurrently",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],4096},{buf[1],4096}},
			.iovcount	= 2,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 8192 bytes size each, concurrently",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],8192},{buf[1],8192}},
			.iovcount	= 2,
			.total		= 16384,
			.want		= 16384,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each, concurrently",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 16384,
			.want		= 12289,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each, with totla 10KB, concurrently",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 2,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 10240,
			.want		= 10240,
		},
		{
			.name		= "1 vector with 1 byte size, concurrently(x4)",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],1}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 1,
		},
		{
			.name		= "1 vector with 32 bytes size, concurrently(x4)",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],32}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 32,
		},
		{
			.name		= "1 vector with 64 bytes size, concurrently(x4)",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],64}},
			.iovcount	= 1,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "1 vector with 4096 bytes size, concurrently(x4)",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],4096}},
			.iovcount	= 1,
			.total		= 4096,
			.want		= 4096,

		},
		{
			.name		= "1 vector with 8192 bytes size, concurrently(x4)",
			.dev		= "readv1",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],8192}},
			.iovcount	= 1,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 1 byte size each, concurrently(x4)",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],1},{buf[1],1}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 2,
		},
		{
			.name		= "2 vectors with 32 bytes size each, concurrently(x4)",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],32},{buf[1],32}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 64,
		},
		{
			.name		= "2 vectors with 64 bytes size each, concurrently(x4)",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],64},{buf[1],64}},
			.iovcount	= 2,
			.total		= 1024,
			.want		= 128,
		},
		{
			.name		= "2 vectors with 4096 bytes size each, concurrently(x4)",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],4096},{buf[1],4096}},
			.iovcount	= 2,
			.total		= 8192,
			.want		= 8192,
		},
		{
			.name		= "2 vectors with 8192 bytes size each, concurrently(x4)",
			.dev		= "readv2",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],8192},{buf[1],8192}},
			.iovcount	= 2,
			.total		= 16384,
			.want		= 16384,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each, concurrently(x4)",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 16384,
			.want		= 12289,
		},
		{
			.name		= "3 vectors with 1, 4096, and 8192 bytes size each, with totla 10KB, concurrently(x4)",
			.dev		= "readv3",
			.flags		= O_RDONLY,
			.concurrent	= 4,
			.iov		= {{buf[0],1},{buf[0]+4096,4096},{buf[2],8192}},
			.iovcount	= 3,
			.total		= 10240,
			.want		= 10240,
		},
		{.name = NULL}, /* sentry */
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
		} else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(errno));
			goto err;
		}
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(errno));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: does not exit\n", t->name);
			goto err;
		}
		if (WEXITSTATUS(status))
			goto err;
		ksft_inc_pass_cnt();
		continue;
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
