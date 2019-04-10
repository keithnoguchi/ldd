/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

int main(void)
{
	const struct test {
		const char	*const name;
		const char	*const dev;
		int		flags;
		int		count;
		int		want;
	} *t, tests[] = {
		{
			.name	= "1 open(open0, O_RDONLY) call",
			.dev	= "/dev/open0",
			.flags	= O_RDONLY,
			.count	= 1,
			.want	= 0,
		},
		{
			.name	= "10 open(open0, O_RDONLY) calls",
			.dev	= "/dev/open0",
			.flags	= O_RDONLY,
			.count	= 10,
			.want	= 0,
		},
		{
			.name	= "1 open(open1, O_RDONLY) call",
			.dev	= "/dev/open1",
			.flags	= O_RDONLY,
			.count	= 1,
			.want	= 0,
		},
		{
			.name	= "10 open(open1, O_RDONLY) calls",
			.dev	= "/dev/open1",
			.flags	= O_RDONLY,
			.count	= 10,
			.want	= 0,
		},
		{
			.name	= "1 open(open0, O_WRONLY) call",
			.dev	= "/dev/open0",
			.flags	= O_WRONLY,
			.count	= 1,
			.want	= 0,
		},
		{
			.name	= "10 open(open0, O_WRONLY) calls",
			.dev	= "/dev/open0",
			.flags	= O_WRONLY,
			.count	= 10,
			.want	= 0,
		},
		{
			.name	= "1 open(open1, O_WRONLY) call",
			.dev	= "/dev/open1",
			.flags	= O_WRONLY,
			.count	= 1,
			.want	= 0,
		},
		{
			.name	= "10 open(open1, O_WRONLY) calls",
			.dev	= "/dev/open1",
			.flags	= O_WRONLY,
			.count	= 10,
			.want	= 0,
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
		} else if (pid == 0) {
			int i, ret;
			for (i = 0; i < t->count; i++) {
				ret = open(t->dev, t->flags);
				if (ret == -1) {
					exit(EXIT_FAILURE);
				}
			}
			exit(EXIT_SUCCESS);
		}
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
		if (WEXITSTATUS(status) != t->want) {
			fprintf(stderr, "%s: unexpected exit status:\n\t- want: %d\n\t-  got: %d\n",
				t->name, t->want, WEXITSTATUS(status));
			goto fail;
		}
		ksft_inc_pass_cnt();
		continue;
fail:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
