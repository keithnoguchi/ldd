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
	const char	*const dev;
	int		flags;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	int ret, fd;

	if (!t->path)
		goto dev;
	fd = open(t->path, t->flags);
	if (fd == -1) {
		fprintf(stderr, "%s: open(%s) %s\n", t->name,
			t->path, strerror(errno));
		goto err;
	}
	if (close(fd) == -1) {
		fprintf(stderr, "%s: close(%s) %s\n", t->name,
			t->path, strerror(errno));
		goto err;
	}
dev:
	if (!t->dev)
		goto done;
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0) {
		fprintf(stderr, "%s: snprintf(%s) %s\n", t->name,
			path, strerror(errno));
		goto err;
	}
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "%s: open(%s) %s\n", t->name,
			path, strerror(errno));
		goto err;
	}
done:
	exit(EXIT_SUCCESS);
err:
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "/proc/driver/seq directory",
			.path	= "/proc/driver/seq",
			.flags	= O_RDONLY|O_DIRECTORY,
		},
		{
			.name	= "/proc/driver/seq/devices directory",
			.path	= "/proc/driver/seq/devices",
			.flags	= O_RDONLY|O_DIRECTORY,
		},
		{
			.name	= "/proc/driver/seq/devices/all file",
			.path	= "/proc/driver/seq/devices/all",
			.flags	= O_RDONLY,
		},
		{
			.name	= "/dev/seq0 device existance",
			.dev	= "seq0",
		},
		{
			.name	= "/dev/seq1 device existance",
			.dev	= "seq1",
		},
		{.name = NULL}, /* sentry */
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			perror("fork");
			goto err;
		} else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1) {
			perror("waitpid");
			goto err;
		}
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: child signaled with %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: child does not exit\n",
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
