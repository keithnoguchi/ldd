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
	char buf[LINE_MAX];
	int ret, fd = -1;
	FILE *fp;

	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, t->flags);
	if (fd == -1)
		goto err;
	ret = close(fd);
	if (ret == -1)
		goto err;
	exit(EXIT_SUCCESS);
err:
	if (errno == t->err)
		exit(EXIT_SUCCESS);
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	if (fd != -1)
		close(fd);
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "open read only",
			.dev	= "write0",
			.flags	= O_RDONLY,
			.err	= EINVAL,
		},
		{
			.name	= "open write only",
			.dev	= "write0",
			.flags	= O_WRONLY,
		},
		{.name = NULL},
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
				t->name, WTERMSIG(status));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: not exited\n",
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