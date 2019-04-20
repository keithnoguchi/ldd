/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const path;
};

static void test(const struct test *restrict t)
{
	FILE *fp;

	fp = fopen(t->path, "r");
	if (fp == NULL)
		goto err;
	exit(EXIT_SUCCESS);
err:
	fprintf(stderr, "%s: %s\n",
		t->name, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "/proc/driver/proc file",
			.path	= "/proc/driver/proc",
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
			fprintf(stderr, "%s: signaled (%s)\n",
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
