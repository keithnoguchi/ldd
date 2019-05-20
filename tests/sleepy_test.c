/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
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
	unsigned int	readers;
	unsigned int	writers;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	int			start;
};

static void *reader(void *arg)
{
	struct context *ctx = arg;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	return (void *)EXIT_SUCCESS;
}

static void *writer(void *arg)
{
	struct context *ctx = arg;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	return (void *)EXIT_SUCCESS;
}

static void test(const struct test *restrict t)
{
	unsigned int nr = (t->readers+t->writers)*2;
	const struct rlimit limit = {
		.rlim_cur	= nr > 1024 ? nr : 1024,
		.rlim_max	= nr > 1024 ? nr : 1024,
	};
	struct context ctx = {
		.t	= t,
		.lock	= PTHREAD_MUTEX_INITIALIZER,
		.cond	= PTHREAD_COND_INITIALIZER,
		.start	= 0,
	};
	pthread_t readers[t->readers], writers[t->writers];
	cpu_set_t cpus;
	int i, err;

	err = setrlimit(RLIMIT_NOFILE, &limit);
	if (err == -1)
		goto perr;
	err = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (err == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	memset(readers, 0, sizeof(readers));
	memset(writers, 0, sizeof(writers));
	for (i = 0; i < t->readers; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&readers[i], &attr, reader, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	for (i = 0; i < t->writers; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&writers[i], &attr, writer, &ctx);
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
	err = pthread_yield();
	if (err) {
		errno = err;
		goto perr;
	}
	for (i = 0; i < t->readers; i++) {
		void *retp;
		if (!readers[i])
			continue;
		err = pthread_join(readers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != (void *)EXIT_SUCCESS)
			goto err;
	}
	for (i = 0; i < t->writers; i++) {
		void *retp;
		if (!writers[i])
			continue;
		err = pthread_join(writers[i], &retp);
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
			.name		= "1 reader on sleepy0",
			.dev		= "sleepy0",
			.readers	= 1,
			.writers	= 0,
		},
		{
			.name		= "1 writer on sleepy1",
			.dev		= "sleepy1",
			.readers	= 0,
			.writers	= 1,
		},
		{
			.name		= "1 reader and 1 writer on sleepy0",
			.dev		= "sleepy0",
			.readers	= 1,
			.writers	= 1,
		},
		{
			.name		= "32 readers on sleepy0",
			.dev		= "sleepy0",
			.readers	= 32,
			.writers	= 0,
		},
		{
			.name		= "32 writers on sleepy1",
			.dev		= "sleepy1",
			.readers	= 0,
			.writers	= 32,
		},
		{
			.name		= "32 readers and 32 writers on sleepy0",
			.dev		= "sleepy0",
			.readers	= 32,
			.writers	= 32,
		},
		{
			.name		= "64 readers on sleepy1",
			.dev		= "sleepy1",
			.readers	= 64,
			.writers	= 0,
		},
		{
			.name		= "64 writers on sleepy0",
			.dev		= "sleepy0",
			.readers	= 0,
			.writers	= 64,
		},
		{
			.name		= "64 readers and 64 writers on sleepy1",
			.dev		= "sleepy1",
			.readers	= 64,
			.writers	= 64,
		},
		{
			.name		= "256 readers and 16 writers on sleepy0",
			.dev		= "sleepy0",
			.readers	= 256,
			.writers	= 16,
		},
		{
			.name		= "1024 readers and 256 writers on sleepy1",
			.dev		= "sleepy1",
			.readers	= 1024,
			.writers	= 256,
		},
		{
			.name		= "2048 readers and 512 writers on sleepy0",
			.dev		= "sleepy0",
			.readers	= 2048,
			.writers	= 512,
		},
		{
			.name		= "4096 readers and 1024 writers on sleepy1",
			.dev		= "sleepy1",
			.readers	= 4096,
			.writers	= 1024,
		},
		{.name = NULL},
	};

	for (t = tests; t->name; t++) {
		int err, status;
		pid_t pid;

		pid = fork();
		if (pid == -1)
			goto perr;
		else if (pid == 0)
			test(t);

		err = waitpid(pid, &status, 0);
		if (err == -1)
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
