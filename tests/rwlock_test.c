/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	nr;
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	int		start;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	int fd[t->nr];
	int i, err;

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	memset(fd, 0, sizeof(fd));
	for (i = 0; i < t->nr; i++) {
		fd[i] = open(path, O_RDONLY);
		if (fd[i] == -1)
			goto perr;
	}
	pthread_yield();
	for (i = 0; i < t->nr; i++) {
		if (!fd[i])
			continue;
		if (close(fd[i]) == -1)
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
			.name	= "single thread",
			.dev	= "rwlock0",
			.nr	= 1,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "double threads",
			.dev	= "rwlock1",
			.nr	= 2,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "triple threads",
			.dev	= "rwlock0",
			.nr	= 3,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "quad threads",
			.dev	= "rwlock1",
			.nr	= 4,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "32 threads",
			.dev	= "rwlock0",
			.nr	= 32,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "64 threads",
			.dev	= "rwlock1",
			.nr	= 64,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "128 threads",
			.dev	= "rwlock0",
			.nr	= 128,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "256 threads",
			.dev	= "rwlock1",
			.nr	= 256,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "512 threads",
			.dev	= "rwlock0",
			.nr	= 512,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
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
