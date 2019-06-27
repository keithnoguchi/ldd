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
	const char	*const path;
	unsigned int	delay_ms;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[BUFSIZ];
	FILE *fp;
	int ret;

	ret = snprintf(path, sizeof(path), "/proc/driver/%s", t->path);
	if (ret < 0)
		goto perr;
	ret = snprintf(buf, sizeof(buf), "%d\n", t->delay_ms);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "w");
	if (!fp)
		goto perr;
	ret = fwrite(buf, strlen(buf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "w");
	if (!fp)
		goto perr;
	ret = fputs("0\n", fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fprintf(stdout, "%s:\n%s", t->name, buf);
	exit(EXIT_SUCCESS);
perr:
	perror(t->name);
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name		= "tasklet based 1ms delay",
			.path		= "jitasklet",
			.delay_ms	= 1,
		},
		{
			.name		= "tasklet based 2ms delay",
			.path		= "jitasklet",
			.delay_ms	= 2,
		},
		{
			.name		= "tasklet based 4ms delay",
			.path		= "jitasklet",
			.delay_ms	= 4,
		},
		{
			.name		= "tasklet based 8ms delay",
			.path		= "jitasklet",
			.delay_ms	= 8,
		},
		{
			.name		= "tasklet based 16ms delay",
			.path		= "jitasklet",
			.delay_ms	= 16,
		},
		{
			.name		= "tasklet based 32ms delay",
			.path		= "jitasklet",
			.delay_ms	= 32,
		},
		{
			.name		= "tasklet based 64ms delay",
			.path		= "jitasklet",
			.delay_ms	= 64,
		},
		{
			.name		= "tasklet based 128ms delay",
			.path		= "jitasklet",
			.delay_ms	= 128,
		},
		{
			.name		= "hi tasklet based 1ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 1,
		},
		{
			.name		= "hi tasklet based 2ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 2,
		},
		{
			.name		= "hi tasklet based 4ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 4,
		},
		{
			.name		= "hi tasklet based 8ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 8,
		},
		{
			.name		= "hi tasklet based 16ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 16,
		},
		{
			.name		= "hi tasklet based 32ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 32,
		},
		{
			.name		= "hi tasklet based 64ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 64,
		},
		{
			.name		= "hi tasklet based 128ms delay",
			.path		= "jitasklethi",
			.delay_ms	= 128,
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
