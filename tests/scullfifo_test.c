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
	size_t		rbufsiz;
	size_t		wbufsiz;
	size_t		bufsiz;
	size_t		alloc;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		read_cond;
	pthread_cond_t		write_cond;
	int			read_start;
	int			write_start;
};

static void *reader(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	char path[PATH_MAX], buf[t->rbufsiz];
	size_t rem;
	void *ptr;
	int ret, fd;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->read_start)
		pthread_cond_wait(&ctx->read_cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	rem = t->rbufsiz;
	ptr = buf;
	while (rem) {
		ret = read(fd, ptr, rem);
		if (ret == -1)
			goto perr;
		else if (ret == 0) {
			fprintf(stderr, "%s: premature read finish\n",
				t->name);
			goto err;
		}
		rem -= ret;
		ptr += ret;
	}
	if (close(fd) == -1)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
err:
	return (void *)EXIT_FAILURE;
}

static void *writer(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	char path[PATH_MAX], buf[t->wbufsiz];
	size_t rem;
	void *ptr;
	int ret, fd;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->write_start)
		pthread_cond_wait(&ctx->write_cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		goto perr;
	rem = t->wbufsiz;
	ptr = buf;
	while (rem) {
		ret = write(fd, ptr, rem);
		if (ret == -1)
			goto perr;
		rem -= ret;
		ptr += ret;
	}
	if (close(fd) == -1)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
}

static void test(const struct test *restrict t)
{
	unsigned int nr = (t->readers+t->writers)*2;
	const struct rlimit limit = {
		.rlim_cur	= nr > 1024 ? nr : 1024,
		.rlim_max	= nr > 1024 ? nr : 1024,
	};
	struct context ctx = {
		.t		= t,
		.lock		= PTHREAD_MUTEX_INITIALIZER,
		.read_cond	= PTHREAD_COND_INITIALIZER,
		.write_cond	= PTHREAD_COND_INITIALIZER,
		.read_start	= 0,
		.write_start	= 0,
	};
	pthread_t readers[t->readers], writers[t->writers];
	char path[PATH_MAX], buf[BUFSIZ];
	int i, ret, err;
	cpu_set_t cpus;
	FILE *fp;
	long val;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/readers", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected initial readers count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/writers", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected initial writers count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/bufsiz", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r+");
	if (!fp)
		goto perr;
	ret = snprintf(buf, sizeof(buf), "%ld\n", t->bufsiz);
	if (ret < 0)
		goto perr;
	ret = fwrite(buf, sizeof(buf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/alloc", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != t->alloc) {
		fprintf(stderr, "%s: unexpected alloc value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->alloc, val);
		goto err;
	}
	ret = setrlimit(RLIMIT_NOFILE, &limit);
	if (ret == -1)
		goto perr;
	CPU_ZERO(&cpus);
	ret = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (ret == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	for (i = 0; i < t->readers; i++) {
		pthread_attr_t attr;
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
	ctx.write_start = 1;
	err = pthread_cond_broadcast(&ctx.write_cond);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_mutex_unlock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
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
	err = pthread_mutex_lock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	ctx.read_start = 1;
	err = pthread_cond_broadcast(&ctx.read_cond);
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
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/readers", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected final readers count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/writers", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected final writers count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/bufsiz", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != t->bufsiz) {
		fprintf(stderr, "%s: unexpected final bufsiz value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->bufsiz, val);
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
			.rbufsiz	= 4095,
			.wbufsiz	= 4095,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "32 readers and 32 writers on scullfifo1",
			.dev		= "scullfifo1",
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 127,
			.wbufsiz	= 127,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "64 readers and 64 writers on scullfifo0",
			.dev		= "scullfifo0",
			.readers	= 64,
			.writers	= 64,
			.rbufsiz	= 63,
			.wbufsiz	= 63,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "256 readers and 256 writers on scullfifo1",
			.dev		= "scullfifo1",
			.readers	= 256,
			.writers	= 256,
			.rbufsiz	= 15,
			.wbufsiz	= 15,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 1023,
			.wbufsiz	= 1023,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 8191,
			.wbufsiz	= 8191,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "1 reader and 2 writers on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 1022,
			.wbufsiz	= 511,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 2 writers on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8190,
			.wbufsiz	= 4095,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "2 readers and 1 writer on scullfifo0 with 1KB buffer",
			.dev		= "scullfifo0",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 511,
			.wbufsiz	= 1022,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "2 readers and 1 writer on scullfifo1 with 8KB buffer",
			.dev		= "scullfifo1",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4095,
			.wbufsiz	= 8190,
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
