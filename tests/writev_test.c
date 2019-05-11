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
#include <sys/wait.h>
#include <sys/uio.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	int		err;
	struct iovec	iov[6];
	size_t		iovcnt;
	size_t		total;
	size_t		each;
	size_t		want;
};

static void test(const struct test *restrict t)
{
	char buf[PATH_MAX];
	int fd, ret;
	FILE *fp;
	long val;

	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, t->flags);
	if (fd == -1)
		goto err;
	if (t->err) {
		fprintf(stderr, "%s: unexpected success\n", t->name);
		exit(EXIT_FAILURE);
	}
	ret = writev(fd, t->iov, t->iovcnt);
	if (ret == -1)
		goto err;
	ret = snprintf(buf, sizeof(buf), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto err;
	fp = fopen(buf, "r");
	if (fp == NULL)
		goto err;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto err;
	val = strtol(buf, NULL, 10);
	if (val < 0 || val > LONG_MAX) {
		fprintf(stderr, "%s: wrong file size (%ld)\n",
			t->name, val);
		exit(EXIT_FAILURE);
	}
	if (val != t->want) {
		fprintf(stderr, "%s: unexpected file size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->want, val);
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
err:
	if (t->err && errno == t->err)
		exit(EXIT_SUCCESS);
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(void)
{
	char buf[4][4096];
	const struct test *t, tests[] = {
		{
			.name	= "open writev0 read-only",
			.dev	= "writev0",
			.flags	= O_RDONLY,
			.err	= EINVAL,
		},
		{
			.name	= "open writev1 write-only and 1 1024 buf",
			.dev	= "writev1",
			.flags	= O_WRONLY,
			.iov	= {{buf[0],1024}},
			.iovcnt	= 1,
			.want	= 1024,
		},
		{
			.name	= "open writev2 read-write with 1 1024 buf",
			.dev	= "writev2",
			.flags	= O_RDWR,
			.iov	= {{buf[0],1024}},
			.iovcnt	= 1,
			.want	= 1024,
		},
		{
			.name	= "open writev3 write-only with truncation, 1 1024 buf",
			.dev	= "writev3",
			.flags	= O_WRONLY|O_TRUNC,
			.iov	= {{buf[0],1024}},
			.iovcnt	= 1,
			.want	= 1024,
		},
		{
			.name	= "open writev0 write-only with truncation, 2 1024 bufs",
			.dev	= "writev0",
			.flags	= O_WRONLY|O_TRUNC,
			.iov	= {{buf[0],1024},{buf[1],1024}},
			.iovcnt	= 2,
			.want	= 2048,
		},
		{
			.name	= "open writev1 write-only with truncation, 3 1024 bufs",
			.dev	= "writev1",
			.flags	= O_WRONLY|O_TRUNC,
			.iov	= {{buf[0],1024},{buf[1],1024},{buf[2],1024}},
			.iovcnt	= 3,
			.want	= 3072,
		},
		{
			.name	= "open writev2 write-only with truncation, 4 4096 bufs",
			.dev	= "writev2",
			.flags	= O_WRONLY|O_TRUNC,
			.iov	= {{buf[0],4096},{buf[1],4096},{buf[2],4096},{buf[3],4096}},
			.iovcnt	= 4,
			.want	= 16384,
		},
		{
			.name	= "open writev3 write-only with truncation, 1,1024,2048,4096 bufs",
			.dev	= "writev3",
			.flags	= O_WRONLY|O_TRUNC,
			.iov	= {{buf[0],1},{buf[1],1024},{buf[2],2048},{buf[3],4096}},
			.iovcnt	= 4,
			.want	= 7169,
		},
		{
			.name	= "open writev3 write-only continuation, 1,1024,2048,4095 bufs",
			.dev	= "writev3",
			.flags	= O_WRONLY,
			.iov	= {{buf[0],1},{buf[1],1024},{buf[2],2048},{buf[3],4095}},
			.iovcnt	= 4,
			.want	= 7169,
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
			goto err;
		} else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto err;
		}
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: signaled with %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: not exit\n",
				t->name);
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
