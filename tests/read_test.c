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
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	size_t		total;
	size_t		size;
	int		want;
};

static void test(const struct test *restrict t)
{
	char buf[LINE_MAX];
	int i, ret, fd;
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
	ret = fwrite(buf, strlen(buf), 1, fp);
	if (ret != 1)
		goto err;
	ret = fclose(fp);
	if (ret == -1)
		goto err;
	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, O_RDONLY);
	if (fd == -1)
		goto err;
	for (i = 0; (ret = read(fd, buf, t->size)) > 0; i++)
		;
	if (ret == -1)
		goto err;
	if (i != t->want) {
		fprintf(stderr, "%s: unexpected result:\n\t- want: %d\n\t-  got: %d\n",
			t->name, t->want, i);
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
err:
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "read on read0(total 2) each 1 byte",
			.dev	= "read0",
			.total	= 2,
			.size	= 1,
			.want	= 2,
		},
		{
			.name	= "read on read0(total 2) each 2 byte",
			.dev	= "read0",
			.total	= 2,
			.size	= 2,
			.want	= 1,
		},
		{
			.name	= "read on read0(total 2) each 3 byte",
			.dev	= "read0",
			.total	= 2,
			.size	= 3,
			.want	= 1,
		},
		{
			.name	= "read on read0 each 0 byte",
			.dev	= "read0",
			.total	= 4096,
			.size	= 0,
			.want	= 0,
		},
		{
			.name	= "read on read0 each 1 byte",
			.dev	= "read0",
			.total	= 4096,
			.size	= 1,
			.want	= 4096,
		},
		{
			.name	= "read on read0 each 2 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 2,
			.want	= 2048,
		},
		{
			.name	= "read on read0 each 3 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 3,
			.want	= 1366,
		},
		{
			.name	= "read on read0 each 15 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 15,
			.want	= 274,
		},
		{
			.name	= "read on read0 each 16 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 16,
			.want	= 256,
		},
		{
			.name	= "read on read0 each 17 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 17,
			.want	= 241,
		},
		{
			.name	= "read on read0 each 31 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 31,
			.want	= 133,
		},
		{
			.name	= "read on read0 each 32 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 32,
			.want	= 128,
		},
		{
			.name	= "read on read0 each 33 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 33,
			.want	= 125,
		},
		{
			.name	= "read on read0 each 4095 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 4095,
			.want	= 2,
		},
		{
			.name	= "read on read0 each 4096 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 4096,
			.want	= 1,
		},
		{
			.name	= "read on read0 each 4097 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 4097,
			.want	= 1,
		},
		{
			.name	= "read on read0 each 8191 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 8191,
			.want	= 1,
		},
		{
			.name	= "read on read0 each 8192 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 8192,
			.want	= 1,
		},
		{
			.name	= "read on read0 each 8193 bytes",
			.dev	= "read0",
			.total	= 4096,
			.size	= 8193,
			.want	= 1,
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
			fprintf(stderr, "%s: does not exit\n",
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
