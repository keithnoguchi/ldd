/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	int		open_nr;
	int		fork_nr;
	int		close_nr;
	int		want_nr;
	int		want;
};

static void msleep(long msec)
{
	int ret = poll(NULL, 0, msec);
	if (ret == -1)
		perror("poll");
}

static void test(const struct test *const t)
{
	int status = EXIT_FAILURE;
	pid_t pids[t->fork_nr];
	int fds[t->open_nr];
	char buf[LINE_MAX];
	int i, ret;
	FILE *fp;
	long val;

	sprintf(buf, "/dev/%s", t->dev);
	for (i = 0; i < t->open_nr; i++) {
		ret = open(buf, t->flags);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto fail;
		}
		fds[i] = ret;
	}
	for (i = 0; i < t->fork_nr; i++) {
		pid_t pid = fork();
		if (pid == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto fail;
		} else if (pid == 0) {
			/* 10 sleep */
			char *const argv[] = {"sleep", "10", NULL};
			ret = execvp(argv[0], argv);
			if (ret == -1) {
				fprintf(stderr, "%s: %s\n",
					t->name, strerror(errno));
				goto fail;
			}
			/* not reach */
		}
		pids[i] = pid;
	}
	for (i = 0; i < t->close_nr; i++) {
		ret = close(fds[i]);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto fail;
		}
	}
	/* wait for 10ms for O_CLOEXEC behavior */
	msleep(10);
	sprintf(buf, "/sys/devices/%s/open_nr", t->dev);
	fp = fopen(buf, "r");
	if (fp == NULL) {
		fprintf(stderr, "%s: %s\n",
			t->name, strerror(errno));
		goto fail;
	}
	ret = fread(buf, LINE_MAX, 1, fp);
	if (ret < 0) {
		fprintf(stderr, "%s: %s\n",
			t->name, strerror(errno));
		goto fail;
	}
	val = strtol(buf, NULL, 10);
	if (val < 0 || val > INT_MAX) {
		fprintf(stderr, "%s: wrong value in open_nr\n",
			t->name);
		goto fail;
	}
	if (val != t->want_nr) {
		fprintf(stderr, "%s: unexpected open_nr value:\n\t- want: %d\n\t-  got: %d\n",
			t->name, t->want_nr, val);
		goto fail;
	}
	status = EXIT_SUCCESS;
fail:
	for (i = 0; i < t->fork_nr; i++) {
		kill(pids[i], SIGKILL);
		waitpid(pids[i], NULL, 0);
	}
	exit(status);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name		= "1 open(open0, O_RDONLY) call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 0,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY) calls",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 10,
			.fork_nr	= 0,
			.close_nr	= 0,
			.want_nr	= 10,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_WRONLY) call",
			.dev		= "open0",
			.flags		= O_WRONLY,
			.open_nr	= 1,
			.fork_nr	= 0,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_WRONLY) calls",
			.dev		= "open0",
			.flags		= O_WRONLY,
			.open_nr	= 10,
			.fork_nr	= 0,
			.close_nr	= 0,
			.want_nr	= 10,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY) and 1 close() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 0,
			.close_nr	= 1,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY) and 10 close() calls",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 10,
			.fork_nr	= 0,
			.close_nr	= 10,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_WRONLY) and 1 close() call",
			.dev		= "open0",
			.flags		= O_WRONLY,
			.open_nr	= 1,
			.fork_nr	= 0,
			.close_nr	= 1,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_WRONLY) and 10 close() calls",
			.dev		= "open0",
			.flags		= O_WRONLY,
			.open_nr	= 10,
			.fork_nr	= 0,
			.close_nr	= 10,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY) and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 1,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY) and 10 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 10,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY), 1 close() and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 1,
			.close_nr	= 1,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY), 1 close() and 10 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 1,
			.fork_nr	= 10,
			.close_nr	= 1,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY) and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 10,
			.fork_nr	= 1,
			.close_nr	= 0,
			.want_nr	= 10,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY), 10 close() and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY,
			.open_nr	= 10,
			.fork_nr	= 1,
			.close_nr	= 10,
			.want_nr	= 10,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY|O_CLOEXEC) and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 1,
			.fork_nr	= 1,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY|O_CLOEXEC) and 10 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 1,
			.fork_nr	= 10,
			.close_nr	= 0,
			.want_nr	= 1,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY|O_CLOEXEC), 1 close() and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 1,
			.fork_nr	= 1,
			.close_nr	= 1,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "1 open(open0, O_RDONLY|O_CLOEXEC), 1 close() and 10 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 1,
			.fork_nr	= 10,
			.close_nr	= 1,
			.want_nr	= 0,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY|O_CLOEXEC) and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 10,
			.fork_nr	= 1,
			.close_nr	= 0,
			.want_nr	= 10,
			.want		= 0,
		},
		{
			.name		= "10 open(open0, O_RDONLY|O_CLOEXEC), 10 close() and 1 fork() call",
			.dev		= "open0",
			.flags		= O_RDONLY|O_CLOEXEC,
			.open_nr	= 10,
			.fork_nr	= 1,
			.close_nr	= 10,
			.want_nr	= 0,
			.want		= 0,
		},
		{.name = NULL}, /* sentry */
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			fprintf(stderr, "%s: %s", t->name, strerror(errno));
			goto fail;
		} else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(errno));
			goto fail;
		}
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: child signaled\n",
				t->name);
			goto fail;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: child did not exit\n",
				t->name);
			goto fail;
		}
		if (WEXITSTATUS(status) != t->want)
			goto fail;
		ksft_inc_pass_cnt();
		continue;
fail:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
