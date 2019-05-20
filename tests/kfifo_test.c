/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <poll.h>
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
	unsigned int	nr;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	int			start;
};

static void msleep(unsigned int msec)
{
	if (poll(NULL, 0, msec) == -1)
		perror("poll");
}

static void *tester(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
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

static void test(const struct test *restrict t)
{
	unsigned int nr = t->nr*2;
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
	pthread_t testers[t->nr];
	char buf[BUFSIZ], path[PATH_MAX];
	cpu_set_t cpus;
	int i, err;
	FILE *fp;
	long got;

	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/fifo/used",
		       t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: initial fifo is not empty\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	err = setrlimit(RLIMIT_NOFILE, &limit);
	if (err == -1)
		goto perr;
	CPU_ZERO(&cpus);
	err = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (err == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	memset(testers, 0, sizeof(testers));
	for (i = 0; i < t->nr; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&testers[i], &attr, tester, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	err = pthread_yield();
	if (err) {
		errno = err;
		goto perr;
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
	for (i = 0; i < t->nr; i++) {
		void *retp = (void *)EXIT_FAILURE;
		if (!testers[i])
			continue;
		err = pthread_join(testers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != EXIT_SUCCESS)
			goto err;
	}
	/* wait for the kthread cleansup the queue */
	msleep(50);
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/fifo/used",
		       t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: final fifo is not empty\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/active", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected final active count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/free", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	got = strtol(buf, NULL, 10);
	printf("%24s: %3ld context(s) on free list\n", t->name, got);
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
			.name	= "1 thread(s) on kfifo0",
			.dev	= "kfifo0",
			.nr	= 1,
		},
		{
			.name	= "2 thread(s) on kfifo1",
			.dev	= "kfifo1",
			.nr	= 2,
		},
		{
			.name	= "3 thread(s) on kfifo0",
			.dev	= "kfifo0",
			.nr	= 3,
		},
		{
			.name	= "4 thread(s) on kfifo1",
			.dev	= "kfifo1",
			.nr	= 4,
		},
		{
			.name	= "32 thread(s) on kfifo0",
			.dev	= "kfifo0",
			.nr	= 32,
		},
		{
			.name	= "64 thread(s) on kfifo1",
			.dev	= "kfifo1",
			.nr	= 64,
		},
		{
			.name	= "128 thread(s) on kfifo0",
			.dev	= "kfifo0",
			.nr	= 128,
		},
		{
			.name	= "256 thread(s) on kfifo1",
			.dev	= "kfifo1",
			.nr	= 256,
		},
		{
			.name	= "512 thread(s) on kfifo0",
			.dev	= "kfifo0",
			.nr	= 512,
		},
		{
			.name	= "1024 thread(s) on kfifo0",
			.dev	= "kfifo1",
			.nr	= 1024,
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
