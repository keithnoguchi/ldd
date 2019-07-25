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
	size_t		len;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[t->len];
	int ret, fd;

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		goto perr;
	ret = write(fd, buf, t->len);
	if (ret == -1)
		goto perr;
	if (ret != t->len) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n\t-  got: %d\n",
			t->name, t->len, ret);
		goto err;
	}
	if (close(fd) == -1)
		goto perr;
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
			.name	= "write 4 bytes to scullc0",
			.dev	= "scullc0",
			.len	= 4096,
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
			fprintf(stderr, "%s: signaled by %s\n",
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
