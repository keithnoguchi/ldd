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
	int		flags;
	int		err;
};

static void test(const struct test *restrict t)
{
	char buf[PATH_MAX];
	int fd, ret;

	ret = snprintf(buf, sizeof(buf)-1, "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, t->flags);
	if (fd == -1)
		goto err;
	if (t->err) {
		fprintf(stderr, "%s: unexpected success\n", t->name);
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
	const struct test *t, tests[] = {
		{
			.name	= "open writev0 read-only",
			.dev	= "writev0",
			.flags	= O_RDONLY,
			.err	= EINVAL,
		},
		{
			.name	= "open writev1 write-only",
			.dev	= "writev1",
			.flags	= O_WRONLY,
		},
		{
			.name	= "open writev2 read-write",
			.dev	= "writev2",
			.flags	= O_RDWR,
		},
		{
			.name	= "open writev3 write-only with truncation",
			.dev	= "writev3",
			.flags	= O_WRONLY|O_TRUNC,
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
