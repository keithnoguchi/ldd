/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
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

static void *test(void *arg)
{
	struct test *t = arg;
	char path[PATH_MAX];
	int err, fd;

	pthread_mutex_lock(&t->lock);
	while (!t->start)
		pthread_cond_wait(&t->cond, &t->lock);
	pthread_mutex_unlock(&t->lock);

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	err = pthread_yield();
	if (err) {
		errno = err;
		goto perr;
	}
	if (close(fd) == -1)
		goto perr;
	pthread_exit((void *)EXIT_SUCCESS);
perr:
	perror(t->name);
	pthread_exit((void *)EXIT_FAILURE);
}

static void tester(struct test *t)
{
	pthread_t lockers[t->nr];
	void *retp = NULL;
	int i, err;

	memset(lockers, 0, sizeof(lockers));
	for (i = 0; i < t->nr; i++) {
		err = pthread_create(&lockers[i], NULL, test, (void *)t);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	err = pthread_mutex_lock(&t->lock);
	if (err) {
		errno = err;
		goto perr;
	}
	t->start = 1;
	err = pthread_mutex_unlock(&t->lock);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_cond_broadcast(&t->cond);
	if (err) {
		errno = err;
		goto perr;
	}
	for (i = 0; i < t->nr; i++) {
		if (!lockers[i])
			continue;
		err = pthread_join(lockers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != NULL)
			goto err;
	}
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
			.name	= "single thread",
			.dev	= "spinlock0",
			.nr	= 1,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "double threads",
			.dev	= "spinlock1",
			.nr	= 2,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "triple threads",
			.dev	= "spinlock0",
			.nr	= 3,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "quad threads",
			.dev	= "spinlock1",
			.nr	= 4,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "32 threads",
			.dev	= "spinlock0",
			.nr	= 32,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "64 threads",
			.dev	= "spinlock1",
			.nr	= 64,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "128 threads",
			.dev	= "spinlock0",
			.nr	= 128,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "256 threads",
			.dev	= "spinlock1",
			.nr	= 256,
			.lock	= PTHREAD_MUTEX_INITIALIZER,
			.cond	= PTHREAD_COND_INITIALIZER,
			.start	= 0,
		},
		{
			.name	= "512 threads",
			.dev	= "spinlock0",
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
			tester((struct test *)t);

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
