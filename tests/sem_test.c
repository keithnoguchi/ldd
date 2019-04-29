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
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	int err, fd;

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, t->flags);
	if (fd == -1)
		goto perr;
	exit(EXIT_SUCCESS);
perr:
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "sem0 O_RDONLY open",
			.dev	= "sem0",
			.flags	= O_RDONLY,
		},
		{
			.name	= "sem0 O_WRONLY open",
			.dev	= "sem0",
			.flags	= O_WRONLY,
		},
		{
			.name	= "sem0 O_RDWR open",
			.dev	= "sem0",
			.flags	= O_RDWR,
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
			fprintf(stderr, "%s: %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: returns failed exit\n", t->name);
			goto err;
		}
		ksft_inc_pass_cnt();
		continue;
perr:
		fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}