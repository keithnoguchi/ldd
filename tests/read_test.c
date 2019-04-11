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
	size_t		count;
	int		want;
};

static void test(const struct test *restrict t)
{
	char buf[LINE_MAX];
	int ret, fd;

	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, O_RDONLY);
	if (fd == -1)
		goto err;
	while ((ret = read(fd, buf, t->count)) > 0)
		;
	if (ret == -1)
		goto err;
	exit(EXIT_SUCCESS);
err:
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "read on read0 each 0 byte",
			.dev	= "read0",
			.count	= 0,
		},
		{
			.name	= "read on read0 each 1 byte",
			.dev	= "read0",
			.count	= 1,
		},
		{
			.name	= "read on read0 each 2 bytes",
			.dev	= "read0",
			.count	= 2,
		},
		{
			.name	= "read on read0 each 3 bytes",
			.dev	= "read0",
			.count	= 3,
		},
		{
			.name	= "read on read0 each 15 bytes",
			.dev	= "read0",
			.count	= 15,
		},
		{
			.name	= "read on read0 each 16 bytes",
			.dev	= "read0",
			.count	= 16,
		},
		{
			.name	= "read on read0 each 17 bytes",
			.dev	= "read0",
			.count	= 17,
		},
		{
			.name	= "read on read0 each 31 bytes",
			.dev	= "read0",
			.count	= 15,
		},
		{
			.name	= "read on read0 each 32 bytes",
			.dev	= "read0",
			.count	= 16,
		},
		{
			.name	= "read on read0 each 33 bytes",
			.dev	= "read0",
			.count	= 17,
		},
		{
			.name	= "read on read0 each 4095 bytes",
			.dev	= "read0",
			.count	= 4095,
		},
		{
			.name	= "read on read0 each 4096 bytes",
			.dev	= "read0",
			.count	= 4096,
		},
		{
			.name	= "read on read0 each 4097 bytes",
			.dev	= "read0",
			.count	= 4097,
		},
		{
			.name	= "read on read0 each 8191 bytes",
			.dev	= "read0",
			.count	= 8191,
		},
		{
			.name	= "read on read0 each 8192 bytes",
			.dev	= "read0",
			.count	= 8192,
		},
		{
			.name	= "read on read0 each 8193 bytes",
			.dev	= "read0",
			.count	= 8193,
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
