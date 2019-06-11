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
	unsigned int	pollers;
	unsigned int	readers;
	unsigned int	writers;
	size_t		rbufsiz;
	size_t		wbufsiz;
	size_t		bufsiz;
	size_t		alloc;
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
	unsigned int nr = t->readers+t->writers;
	size_t rremains[t->readers], wremains[t->writers];
	int readers[t->readers], writers[t->writers];
	char path[PATH_MAX];
	fd_set rfds, wfds;
	int nfds = 0;
	int i, ret;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	FD_ZERO(&rfds);
	memset(readers, 0, sizeof(readers));
	for (i = 0; i < t->readers; i++) {
		readers[i] = open(path, O_RDONLY|O_NONBLOCK);
		if (readers[i] == -1)
			goto perr;
		rremains[i] = t->rbufsiz;
		FD_SET(readers[i], &rfds);
		if (nfds < readers[i])
			nfds = readers[i];
	}
	FD_ZERO(&wfds);
	memset(writers, 0, sizeof(writers));
	for (i = 0; i < t->writers; i++) {
		writers[i] = open(path, O_WRONLY|O_NONBLOCK);
		if (writers[i] == -1)
			goto perr;
		wremains[i] = t->wbufsiz;
		FD_SET(writers[i], &wfds);
		if (nfds < writers[i])
			nfds = writers[i];
	}
	nr = t->readers+t->writers;
	while (nr) {
		ret = select(nfds+1, &rfds, &wfds, NULL, NULL);
		if (ret == -1)
			goto perr;
		for (i = 0; i < t->readers; i++) {
			char rbuf[t->rbufsiz];
			if (readers[i] == 0 || readers[i] == -1)
				continue;
			if (!FD_ISSET(readers[i], &rfds))
				continue;
			ret = read(readers[i], rbuf, rremains[i]);
			if (ret == -1) {
				if (errno == EAGAIN)
					continue;
				goto perr;
			}
			rremains[i] -= ret;
			if (rremains[i])
				continue;
			if (close(readers[i]) == -1)
				goto perr;
			readers[i] = -1;
			nr -= 1;
		}
		for (i = 0; i < t->writers; i++) {
			char wbuf[t->wbufsiz];
			if (writers[i] == 0 || writers[i] == -1)
				continue;
			if (!FD_ISSET(writers[i], &wfds))
				continue;
			ret = write(writers[i], wbuf, wremains[i]);
			if (ret == -1) {
				if (errno == EAGAIN)
					continue;
				goto perr;
			}
			wremains[i] -= ret;
			if (wremains[i])
				continue;
			if (close(writers[i]) == -1)
				goto perr;
			writers[i] = -1;
			nr -= 1;
		}
		FD_ZERO(&rfds);
		nfds = 0;
		for (i = 0; i < t->readers; i++) {
			if (readers[i] == 0 || readers[i] == -1)
				continue;
			FD_SET(readers[i], &rfds);
			if (nfds < readers[i])
				nfds = readers[i];
		}
		FD_ZERO(&wfds);
		for (i = 0; i < t->writers; i++) {
			if (writers[i] == 0 || writers[i] == -1)
				continue;
			FD_SET(writers[i], &wfds);
			if (nfds < writers[i])
				nfds = writers[i];
		}
	}
	for (i = 0; i < t->readers; i++) {
		if (readers[i] == 0 || readers[i] == -1)
			continue;
		if (close(readers[i]) == -1)
			goto perr;
	}
	for (i = 0; i < t->writers; i++) {
		if (writers[i] == 0 || writers[i] == -1)
			continue;
		if (close(writers[i]) == -1)
			goto perr;
	}
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
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
	unsigned int nr = t->pollers*(4+t->readers+t->writers);
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
	char path[PATH_MAX], buf[BUFSIZ];
	pthread_t pollers[t->pollers];
	cpu_set_t cpus;
	int i, ret, err;
	FILE *fp;
	long val;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/bufsiz", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r+");
	if (!fp)
		goto perr;
	ret = snprintf(buf, sizeof(buf), "%ld\n", t->bufsiz);
	if (ret < 0)
		goto perr;
	ret = fwrite(buf, strlen(buf)+1, 1, fp);
	if (ret == -1)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != t->bufsiz) {
		fprintf(stderr, "%s: unexpected initial bufsiz:\n\t- want: %ld\n-  got: %ld\n",
			t->name, t->bufsiz, val);
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
		fprintf(stderr, "%s: unexpected final bufsiz:\n\t- want: %ld\n-  got: %ld\n",
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
			.name		= "1 select(2) poller with 1/1 reader/writer",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 1/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 2/1 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 2/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 2,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 2/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 2,
			.writers	= 4,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 4/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 4,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 4/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 4,
			.writers	= 4,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 16/16 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 16,
			.writers	= 16,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 select(2) poller with 32/32 readers/writers",
			.dev		= "poll0",
			.pollers	= 1,
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 1/1 reader/writer",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 1/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 2/1 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 2/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 2/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 2,
			.writers	= 4,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 4/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 4,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 4/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 4,
			.writers	= 4,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 16/16 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 16,
			.writers	= 16,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "2 select(2) pollers with 32/32 readers/writers",
			.dev		= "poll0",
			.pollers	= 2,
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 1/1 reader/writer",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 1/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 2/1 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 2/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 2,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 2/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 2,
			.writers	= 4,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 4/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 4,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 4/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 4,
			.writers	= 4,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 16/16 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 16,
			.writers	= 16,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "4 select(2) pollers with 32/32 readers/writers",
			.dev		= "poll0",
			.pollers	= 4,
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 1/1 reader/writer",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 1/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 2/1 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 2/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 2,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 2/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 2,
			.writers	= 4,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 4/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 4,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 4/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 4,
			.writers	= 4,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 16/16 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 16,
			.writers	= 16,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "16 select(2) pollers with 32/32 readers/writers",
			.dev		= "poll0",
			.pollers	= 16,
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 1/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 1,
			.writers	= 2,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 2/1 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 2,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 2/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 2,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 2/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 2,
			.writers	= 4,
			.rbufsiz	= 8192,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 4/2 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 4,
			.writers	= 2,
			.rbufsiz	= 4096,
			.wbufsiz	= 8192,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 4/4 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 4,
			.writers	= 4,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 16/16 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 16,
			.writers	= 16,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "32 select(2) pollers with 32/32 readers/writers",
			.dev		= "poll0",
			.pollers	= 32,
			.readers	= 32,
			.writers	= 32,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= selector,
		},
		{
			.name		= "1 poll(2) poller with 1/1 reader/writer",
			.dev		= "poll1",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
			.poller		= poller,
		},
		{
			.name		= "1 epoll(7) poller with 1/1 reader/writer",
			.dev		= "poll2",
			.pollers	= 1,
			.readers	= 1,
			.writers	= 1,
			.rbufsiz	= 4096,
			.wbufsiz	= 4096,
			.bufsiz		= 4096,
			.alloc		= 4096,
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
