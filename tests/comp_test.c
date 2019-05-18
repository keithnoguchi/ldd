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
	unsigned int	waiters;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	int			start;
};

static void *tester(void *arg)
{
	struct context *ctx = arg;
	const struct test *t = ctx->t;
	char path[PATH_MAX];
	int err, fd;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	err = read(fd, NULL, 0);
	if (err < 0)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
}

static void test(const struct test *restrict t)
{
	unsigned int nr = t->waiters*2;
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
	pthread_t waiters[t->waiters];
	char buf[BUFSIZ], path[PATH_MAX];
	cpu_set_t cpus;
	int i, err, fd;
	void *retp;
	FILE *fp;
	long got;

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		goto perr;
	err = setrlimit(RLIMIT_NOFILE, &limit);
	if (err == -1)
		goto perr;
	CPU_ZERO(&cpus);
	err = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (err == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	memset(waiters, 0, sizeof(waiters));
	for (i = 0; i < t->waiters; i++) {
		pthread_attr_t attr;
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus),
						  &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&waiters[i], &attr, tester, &ctx);
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
	/* wait for all the waiters */
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/waiters", t->dev);
	if (err < 0)
		goto perr;
	do {
		pthread_yield();
		fp = fopen(path, "r");
		if (!fp) {
			fprintf(stderr, "%s: %s: %s\n", t->name, path, strerror(errno));
			goto err;
		}
		err = fread(buf, sizeof(buf), 1, fp);
		if (err == 0 && ferror(fp))
			goto perr;
		if (fclose(fp) == -1)
			goto perr;
		got = strtol(buf, NULL, 10);
	} while (got < t->waiters);

	/* notify the completion to all the waiters */
	for (i = 0; i < t->waiters; i++) {
		err = write(fd, NULL, 0);
		if (err == -1)
			goto perr;
	}
	for (i = 0; i < t->waiters; i++) {
		if (!waiters[i])
			continue;
		err = pthread_join(waiters[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp) {
			fprintf(stderr, "%s: waiter exited abnormally\n",
				t->name);
			goto err;
		}
	}
	/* make sure all the waiters are gone */
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/waiters", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "%s: %s: %s\n", t->name, path, strerror(errno));
		goto err;
	}
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected number of waiters:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	if (close(fd) == -1)
		goto perr;
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
			.name		= "single completion waiter",
			.dev		= "comp0",
			.waiters	= 1,
		},
		{
			.name		= "two completion waiters",
			.dev		= "comp1",
			.waiters	= 2,
		},
		{
			.name		= "three completion waiters",
			.dev		= "comp2",
			.waiters	= 3,
		},
		{
			.name		= "four completion waiters",
			.dev		= "comp3",
			.waiters	= 4,
		},
		{
			.name		= "8 completion waiters",
			.dev		= "comp0",
			.waiters	= 8,
		},
		{
			.name		= "16 completion waiters",
			.dev		= "comp1",
			.waiters	= 16,
		},
		{
			.name		= "32 completion waiters",
			.dev		= "comp2",
			.waiters	= 32,
		},
		{
			.name		= "64 completion waiters",
			.dev		= "comp3",
			.waiters	= 64,
		},
		{
			.name		= "128 completion waiters",
			.dev		= "comp0",
			.waiters	= 128,
		},
		{
			.name		= "256 completion waiters",
			.dev		= "comp1",
			.waiters	= 256,
		},
		{
			.name		= "512 completion waiters",
			.dev		= "comp2",
			.waiters	= 512,
		},
		{
			.name		= "768 completion waiters",
			.dev		= "comp3",
			.waiters	= 768,
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
