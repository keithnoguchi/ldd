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
#include <sys/resource.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	readers;
	unsigned int	writers;
	size_t		rbufsiz;
	size_t		wbufsiz;
	size_t		bufsiz;
	size_t		alloc;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	int			start;
};

static void *tester(struct context *ctx, int flags)
{
	const struct test *const t = ctx->t;
	char path[PATH_MAX];
	int ret, fd;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, flags);
	if (fd == -1)
		goto perr;
	if (close(fd) == -1)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
}

static void *reader(void *arg)
{
	return tester(arg, O_RDONLY);
}

static void *writer(void *arg)
{
	return tester(arg, O_WRONLY);
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
	char path[PATH_MAX];
	int i, ret, err;

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	ret = setrlimit(RLIMIT_NOFILE, &limit);
	if (ret == -1)
		goto perr;
	for (i = 0; i < t->readers; i++) {
		err = pthread_create(&readers[i], NULL, reader, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	for (i = 0; i < t->writers; i++) {
		err = pthread_create(&writers[i], NULL, writer, &ctx);
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
	for (i = 0; i < t->readers; i++) {
		void *retp;
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
			.name		= "1 reader and 1 writer on scullfifo0",
			.dev		= "scullfifo0",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "32 readers and 32 writers on scullfifo1",
			.dev		= "scullfifo1",
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "64 readers and 64 writers on scullfifo0",
			.dev		= "scullfifo0",
			.readers	= 64,
			.writers	= 64,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "256 readers and 256 writers on scullfifo1",
			.dev		= "scullfifo1",
			.readers	= 256,
			.writers	= 256,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 1024,
			.wbufsiz	= 1024,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 8192,
			.wbufsiz	= 8192,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "1 reader and 2 writers on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 1024,
			.wbufsiz	= 512,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 2 writers on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "2 readers and 1 writer on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 512,
			.wbufsiz	= 1024,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "2 readers and 1 writer on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{.name = NULL}, /* sentry */
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
