/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const file;
	const char	*const wait_file;
	long		wait_msec;
};

static int test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[512];
	FILE *fp;
	int ret;

	if (t->wait_file) {
		ret = snprintf(path, sizeof(path),
			       "/proc/driver/jit/parameters/%s", t->wait_file);
		if (ret < 0)
			goto perr;
		fp = fopen(path, "w");
		if (!fp)
			goto perr;
		ret = snprintf(buf, sizeof(buf), "%ld\n", t->wait_msec);
		if (ret < 0)
			goto perr;
		ret = fwrite(buf, sizeof(buf), 1, fp);
		if (ret == -1)
			goto perr;
		if (fclose(fp) == -1)
			goto perr;
	}
	ret = snprintf(path, sizeof(path), "/proc/driver/jit/%s", t->file);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	buf[sizeof(buf)-1] = '\0';
	fprintf(stdout, "%s:\n%s\n", t->name, buf);
	if (t->wait_file) {
		/* reset the wait msec */
		ret = snprintf(path, sizeof(path),
			       "/proc/driver/jit/parameters/%s", t->wait_file);
		if (ret < 0)
			goto perr;
		fp = fopen(path, "w");
		if (!fp)
			goto perr;
		ret = snprintf(buf, sizeof(buf), "0\n");
		if (ret < 0)
			goto perr;
		ret = fwrite(buf, sizeof(buf), 1, fp);
		if (ret == -1)
			goto perr;
		if (fclose(fp) == -1)
			goto perr;
	}
	exit(EXIT_SUCCESS);
perr:
	perror(t->name);
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name		= "read hz file",
			.file		= "hz",
		},
		{
			.name		= "read user_hz file",
			.file		= "user_hz",
		},
		{
			.name		= "read currenttime file",
			.file		= "currenttime",
		},
		{
			.name		= "read jitbusy file with 1ms delay",
			.file		= "jitbusy",
			.wait_file	= "busy_wait_msec",
			.wait_msec	= 1,
		},
		{
			.name		= "read jitbusy file with 2ms delay",
			.file		= "jitbusy",
			.wait_file	= "busy_wait_msec",
			.wait_msec	= 2,
		},
		{
			.name		= "read jitbusy file with 4ms delay",
			.file		= "jitbusy",
			.wait_file	= "busy_wait_msec",
			.wait_msec	= 4,
		},
		{
			.name		= "read jitbusy file with 8ms delay",
			.file		= "jitbusy",
			.wait_file	= "busy_wait_msec",
			.wait_msec	= 8,
		},
		{
			.name		= "read jitsched file with 1ms delay",
			.file		= "jitsched",
			.wait_file	= "sched_wait_msec",
			.wait_msec	= 1,
		},
		{
			.name		= "read jitsched file with 2ms delay",
			.file		= "jitsched",
			.wait_file	= "sched_wait_msec",
			.wait_msec	= 2,
		},
		{
			.name		= "read jitsched file with 4ms delay",
			.file		= "jitsched",
			.wait_file	= "sched_wait_msec",
			.wait_msec	= 4,
		},
		{
			.name		= "read jitsched file with 8ms delay",
			.file		= "jitsched",
			.wait_file	= "sched_wait_msec",
			.wait_msec	= 8,
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
