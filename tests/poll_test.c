/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	pollers;
	unsigned int	readers;
	unsigned int	writers;
	void		*(*poller)(void *);
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	unsigned int		start;
};

static void *selector(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);
	printf("selector for %s\n", t->name);
	return (void *)EXIT_SUCCESS;
}

static void *poller(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);
	printf("poller for %s\n", t->name);
	return (void *)EXIT_SUCCESS;
}

static void *epoller(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);
	printf("epoller for %s\n", t->name);
	return (void *)EXIT_SUCCESS;
}

static void test(const struct test *restrict t)
{
	unsigned int nr = t->pollers > 1024 ? t->pollers : 1024;
	const struct rlimit limit = {
		.rlim_cur	= nr,
		.rlim_max	= nr,
	};
	struct context ctx = {
		.t	= t,
		.lock	= PTHREAD_MUTEX_INITIALIZER,
		.cond	= PTHREAD_COND_INITIALIZER,
		.start	= 0,
	};
	pthread_t pollers[t->pollers];
	cpu_set_t cpus;
	int i, ret, err;

	ret = setrlimit(RLIMIT_NOFILE, &limit);
	if (ret == -1)
		goto perr;
	CPU_ZERO(&cpus);
	ret = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (ret == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	memset(pollers, 0, sizeof(pollers));
	for (i = 0; i < t->pollers; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&pollers[i], &attr, t->poller, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	err = pthread_mutex_lock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	ctx.start = 1;
	err = pthread_cond_broadcast(&ctx.cond);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_mutex_unlock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	for (i = 0; i < t->pollers; i++) {
		void *retp;
		err = pthread_join(pollers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != (void *)EXIT_SUCCESS)
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
			.name		= "1 select(2) poller with 1/1 reader/writer",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.poller		= selector,
		},
		{
			.name		= "1 poll(2) poller with 1/1 reader/writer",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.poller		= poller,
		},
		{
			.name		= "1 epoll(7) poller with 1/1 reader/writer",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.poller		= epoller,
		},
		{
			.name		= "2 select(2) pollers with 2/2 readers/writers",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 2,
			.poller		= selector,
		},
		{
			.name		= "2 poll(2) pollers with 2/2 readers/writers",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 2,
			.poller		= poller,
		},
		{
			.name		= "2 epoll(7) pollers with 2/2 readers/writers",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 2,
			.poller		= epoller,
		},
		{
			.name		= "2 select(2) pollers with 4/4 readers/writers",
			.pollers	= 2,
			.readers	= 4,
			.writers	= 4,
			.poller		= selector,
		},
		{
			.name		= "2 poll(2) pollers with 4/4 readers/writers",
			.pollers	= 2,
			.readers	= 4,
			.writers	= 4,
			.poller		= poller,
		},
		{
			.name		= "2 epoll(7) pollers with 4/4 readers/writers",
			.pollers	= 2,
			.readers	= 4,
			.writers	= 4,
			.poller		= epoller,
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
