/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	nr;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	int err, fd;

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	if (close(fd) == -1)
		goto perr;
	exit(EXIT_SUCCESS);
perr:
	perror(t->name);
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "1 thread(s) on seqlock0",
			.dev	= "seqlock0",
			.nr	= 1,
		},
		{
			.name	= "2 thread(s) on seqlock1",
			.dev	= "seqlock1",
			.nr	= 2,
		},
		{
			.name	= "3 thread(s) on seqlock0",
			.dev	= "seqlock0",
			.nr	= 3,
		},
		{
			.name	= "4 thread(s) on seqlock1",
			.dev	= "seqlock1",
			.nr	= 4,
		},
		{
			.name	= "32 thread(s) on seqlock0",
			.dev	= "seqlock0",
			.nr	= 32,
		},
		{
			.name	= "64 thread(s) on seqlock1",
			.dev	= "seqlock1",
			.nr	= 64,
		},
		{
			.name	= "128 thread(s) on seqlock0",
			.dev	= "seqlock0",
			.nr	= 128,
		},
		{
			.name	= "256 thread(s) on seqlock1",
			.dev	= "seqlock1",
			.nr	= 256,
		},
		{
			.name	= "512 thread(s) on seqlock0",
			.dev	= "seqlock0",
			.nr	= 512,
		},
		{
			.name	= "1024 thread(s) on seqlock1",
			.dev	= "seqlock1",
			.nr	= 1024,
		},
		{.name = NULL},
	};

	for (t = tests; t->name; t++) {
		int err, status;
		pid_t pid;

		pid = fork();
		if (pid == -1)
			goto perr;
		else if (pid == 0)
			test(t);

		err = waitpid(pid, &status, 0);
		if (err == -1)
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
