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
	pthread_cond_t		cond;
	int			start;
};

static void *reader(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	char *ptr, buf[t->rbufsiz];
	char path[PATH_MAX];
	int ret, fd, len;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	len = t->rbufsiz;
	ptr = buf;
	while (len > 0) {
		ret = read(fd, ptr, len);
		if (ret == -1)
			goto perr;
		len -= ret;
		ptr += ret;
	}
	if (close(fd) == -1)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
}

static void *writer(void *arg)
{
	struct context *ctx = arg;
	const struct test *const t = ctx->t;
	char *ptr, buf[t->wbufsiz];
	char path[PATH_MAX];
	int ret, fd, len;

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		goto perr;
	len = t->wbufsiz;
	ptr = buf;
	while (len > 0) {
		ret = write(fd, ptr, len);
		if (ret == -1)
			goto perr;
		len -= ret;
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
		.t	= t,
		.lock	= PTHREAD_MUTEX_INITIALIZER,
		.cond	= PTHREAD_COND_INITIALIZER,
		.start	= 0,
	};
	pthread_t readers[t->readers], writers[t->writers];
	char buf[BUFSIZ], path[PATH_MAX];
	cpu_set_t cpus;
	int i, ret, err;
	FILE *fp;
	long got;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/bufsiz", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "w");
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
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp)) {
		fprintf(stderr, "%s: here\n", t->name);
		goto perr;
	}
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != t->bufsiz) {
		fprintf(stderr, "%s: unexpected initial bufsiz:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->bufsiz, got);
		goto err;
	}
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
	got = strtol(buf, NULL, 10);
	if (got != t->alloc) {
		fprintf(stderr, "%s: unexpected initial buffer alloc size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->alloc, got);
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected initial readers:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected initial writers:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/is_empty", t->dev);
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
	got = strtol(buf, NULL, 10);
	if (got != 1) {
		fprintf(stderr, "%s: unexpected initial is_empty value:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/is_full", t->dev);
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected initial is_full value:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = setrlimit(RLIMIT_NOFILE, &limit);
	if (ret == -1)
		goto perr;
	ret = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (ret == -1)
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected final readers:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected final writers:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
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
	got = strtol(buf, NULL, 10);
	if (got != t->bufsiz) {
		fprintf(stderr, "%s: unexpected final bufsiz:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->bufsiz, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/is_empty", t->dev);
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
	got = strtol(buf, NULL, 10);
	if (got != 1) {
		fprintf(stderr, "%s: unexpected final is_empty value:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/is_full", t->dev);
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
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected final is_full value:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, got);
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
			.name		= "1 reader and 1 writer on scullpipe0",
			.dev		= "scullpipe0",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "32 readers and 32 writers on scullpipe1",
			.dev		= "scullpipe1",
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "64 readers and 64 writers on scullpipe0",
			.dev		= "scullpipe0",
			.readers	= 64,
			.writers	= 64,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "256 readers and 256 writers on scullpipe1",
			.dev		= "scullpipe1",
			.readers	= 256,
			.writers	= 256,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullpipe0 with 1KB buffer",
			.dev		= "scullpipe0",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 1024,
			.wbufsiz	= 1024,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 1 writer on scullpipe1 with 8KB buffer",
			.dev		= "scullpipe1",
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 8192,
			.wbufsiz	= 8192,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "1 reader and 2 writers on scullpipe0 with 1KB buffer",
			.dev		= "scullpipe0",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 1024,
			.wbufsiz	= 512,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "1 reader and 2 writers on scullpipe1 with 8KB buffer",
			.dev		= "scullpipe1",
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 8192,
			.alloc		= 8192,
		},
		{
			.name		= "2 readers and 1 writer on scullpipe0 with 1KB buffer",
			.dev		= "scullpipe0",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 512,
			.wbufsiz	= 1024,
			.bufsiz		= 1024,
			.alloc		= 4096,
		},
		{
			.name		= "2 readers and 1 writer on scullpipe1 with 8KB buffer",
			.dev		= "scullpipe1",
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 8192,
			.alloc		= 8192,
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
			fprintf(stderr, "%s: does not exit\n", t->name);
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
