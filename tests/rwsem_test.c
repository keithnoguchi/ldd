/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	readers;
	unsigned int	writers;
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	int		start;
};

static void *test(struct test *t, int flags)
{
	char path[PATH_MAX];
	int err, fd;

	/* wait for the start */
	pthread_mutex_lock(&t->lock);
	while (!t->start)
		pthread_cond_wait(&t->cond, &t->lock);
	pthread_mutex_unlock(&t->lock);

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, flags);
	if (fd == -1)
		goto perr;
	err = pthread_yield();
	if (err) {
		errno = err;
		goto perr;
	}
	if (close(fd) == -1)
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	perror(t->name);
	return (void *)EXIT_FAILURE;
}

static void *reader(void *arg)
{
	return test(arg, O_RDONLY);
}

static void *writer(void *arg)
{
	return test(arg, O_WRONLY);
}

static void tester(struct test *t)
{
	pthread_t rids[t->readers];
	pthread_t wids[t->writers];
	char buf[BUFSIZ], path[PATH_MAX];
	long val, readers, writers;
	int i, err, fail = 0;
	void *retp;
	FILE *fp;

	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lockers",
		       t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected default lockers value:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	memset(rids, 0, sizeof(rids));
	memset(wids, 0, sizeof(wids));
	t->start = 0;
	for (i = 0; i < t->readers; i++) {
		err = pthread_create(&rids[i], NULL, reader, (void *)t);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(err));
			fail++;
			goto join;
		}
	}
	for (i = 0; i < t->writers; i++) {
		err = pthread_create(&wids[i], NULL, writer, (void *)t);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(err));
			fail++;
			goto join;
		}
	}
	/* light the fire */
	pthread_mutex_lock(&t->lock);
	t->start = 1;
	pthread_mutex_unlock(&t->lock);
	pthread_cond_broadcast(&t->cond);
	pthread_yield();
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/readers",
		       t->dev);
	if (err < 0) {
		fail++;
		goto join;
	}
	fp = fopen(path, "r");
	if (fp == NULL) {
		fail++;
		goto join;
	}
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp)) {
		fail++;
		goto join;
	}
	readers = strtol(buf, NULL, 10);
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/writers",
		       t->dev);
	if (err < 0) {
		fail++;
		goto join;
	}
	fp = fopen(path, "r");
	if (fp == NULL) {
		fail++;
		goto join;
	}
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp)) {
		fail++;
		goto join;
	}
	writers = strtol(buf, NULL, 10);
	fprintf(stdout, "%s:\n\treaders: %ld\n\twriters: %ld\n",
		t->name, readers, writers);
join:
	for (i = 0; i < t->readers; i++) {
		if (!rids[i])
			continue;
		err = pthread_join(rids[i], &retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(err));
			fail++;
			continue;
		}
		if (retp != (void *)EXIT_SUCCESS)
			fail++;
	}
	for (i = 0; i < t->writers; i++) {
		if (!wids[i])
			continue;
		err = pthread_join(wids[i], &retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name,
				strerror(err));
			fail++;
			continue;
		}
		if (retp != (void *)EXIT_SUCCESS)
			fail++;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lockers",
		       t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected final lockers value:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	if (fail)
		exit(EXIT_FAILURE);
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
			.name		= "one reader",
			.dev		= "rwsem0",
			.readers	= 1,
			.writers	= 0,
		},
		{
			.name		= "one writer",
			.dev		= "rwsem0",
			.readers	= 0,
			.writers	= 1,
		},
		{
			.name		= "one reader and one writer",
			.dev		= "rwsem0",
			.readers	= 1,
			.writers	= 1,
		},
		{
			.name		= "16 readers",
			.dev		= "rwsem0",
			.readers	= 16,
			.writers	= 0,
		},
		{
			.name		= "16 writers",
			.dev		= "rwsem0",
			.readers	= 0,
			.writers	= 16,
		},
		{
			.name		= "16 readers and 16 writers",
			.dev		= "rwsem0",
			.readers	= 16,
			.writers	= 16,
		},
		{
			.name		= "32 readers",
			.dev		= "rwsem0",
			.readers	= 32,
			.writers	= 0,
		},
		{
			.name		= "32 writers",
			.dev		= "rwsem0",
			.readers	= 0,
			.writers	= 32,
		},
		{
			.name		= "32 readers and 32 writers",
			.dev		= "rwsem0",
			.readers	= 32,
			.writers	= 32,
		},
		{
			.name		= "64 readers",
			.dev		= "rwsem0",
			.readers	= 64,
			.writers	= 0,
		},
		{
			.name		= "64 writers",
			.dev		= "rwsem0",
			.readers	= 0,
			.writers	= 64,
		},
		{
			.name		= "64 readers and 64 writers",
			.dev		= "rwsem0",
			.readers	= 64,
			.writers	= 64,
		},
		{
			.name		= "256 readers and 16 writers",
			.dev		= "rwsem0",
			.readers	= 256,
			.writers	= 16,
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
			fprintf(stderr, "%s: did not exit\n",
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
