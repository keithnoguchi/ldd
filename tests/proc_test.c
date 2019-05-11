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
	const char	*const path;
	int		flags;
	const char	*const want;
};

static void test(const struct test *restrict t)
{
	char buf[LINE_MAX];
	FILE *fp;
	int fd;

	fd = open(t->path, t->flags);
	if (fd == -1)
		goto err;
	if (!t->want)
		goto out;
	fp = fdopen(fd, "r");
	if (fp == NULL)
		goto err;
	fread(buf, LINE_MAX, 1, fp);
	if (ferror(fp))
		goto err;
	if (strcmp(buf, t->want)) {
		fprintf(stderr, "%s: unexpected value:\n\t- want: '%s'\n\t-  got: '%s'\n",
			t->name, t->want, buf);
		exit(EXIT_FAILURE);
	}
out:
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
			.name	= "/proc/driver/proc directory",
			.path	= "/proc/driver/proc",
			.flags	= O_RDONLY|O_DIRECTORY,
			.want	= NULL,
		},
		{
			.name	= "/proc/driver/proc/version file",
			.path	= "/proc/driver/proc/version",
			.flags	= O_RDONLY,
			.want	= "1.0.0\n",
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
