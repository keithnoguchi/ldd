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
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	size_t		reader;
	size_t		writer;
};

static void *test(const struct test *restrict t, int flags)
{
	char path[PATH_MAX];
	char buf[BUFSIZ];
	FILE *fp;
	int err, fd;
	long val;

	err = snprintf(path, sizeof(path), "/sys/module/sem/parameters/default_sem_count");
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 1) {
		fprintf(stderr, "%s: unexpected default semaphore count:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	if (fclose(fp))
		goto perr;
	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, flags);
	if (fd == -1)
		goto perr;
	if (close(fd))
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
err:
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

static void tester(const struct test *restrict t)
{
	pthread_t rid[t->reader];
	pthread_t wid[t->writer];
	char path[PATH_MAX];
	int i, err, *retp;
	int fail = 0;
	long val;

	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lock_nr", t->dev);
	if (err < 0)
		goto perr;
	val = strtol(path, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected beginning lock count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	memset(rid, 0, sizeof(pthread_t)*t->reader);
	memset(wid, 0, sizeof(pthread_t)*t->writer);
	for (i = 0; i < t->reader; i++) {
		err = pthread_create(&rid[i], NULL, reader, (void *)t);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
			goto out;
		}
	}
	for (i = 0; i < t->writer; i++) {
		err = pthread_create(&wid[i], NULL, writer, (void *)t);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
			goto out;
		}
	}
out:
	for (i = 0; i < t->reader; i++) {
		if (!rid[i])
			continue;
		err = pthread_join(rid[i], (void **)&retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
		}
		if (retp != (void *)EXIT_SUCCESS)
			fail++;
	}
	for (i = 0; i < t->writer; i++) {
		if (!wid[i])
			continue;
		err = pthread_join(wid[i], (void **)&retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
		}
		if (retp != (int *)EXIT_SUCCESS)
			fail++;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lock_nr", t->dev);
	if (err < 0)
		goto perr;
	val = strtol(path, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected ending lock count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	if (fail)
		goto err;
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
			.name	= "one reader",
			.dev	= "sem0",
			.reader	= 1,
			.writer	= 0,
		},
		{
			.name	= "one writer",
			.dev	= "sem0",
			.reader	= 0,
			.writer	= 1,
		},
		{
			.name	= "one reader and one writer",
			.dev	= "sem0",
			.reader	= 1,
			.writer	= 1,
		},
		{
			.name	= "16 readers",
			.dev	= "sem0",
			.reader	= 16,
			.writer	= 0,
		},
		{
			.name	= "16 writers",
			.dev	= "sem0",
			.reader	= 0,
			.writer	= 16,
		},
		{
			.name	= "16 readers and 16 writers",
			.dev	= "sem0",
			.reader	= 16,
			.writer	= 16,
		},
		{
			.name	= "32 readers",
			.dev	= "sem0",
			.reader	= 32,
			.writer	= 0,
		},
		{
			.name	= "32 writers",
			.dev	= "sem0",
			.reader	= 0,
			.writer	= 32,
		},
		{
			.name	= "32 readers and 32 writers",
			.dev	= "sem0",
			.reader	= 32,
			.writer	= 32,
		},
		{
			.name	= "64 readers",
			.dev	= "sem0",
			.reader	= 64,
			.writer	= 0,
		},
		{
			.name	= "64 writers",
			.dev	= "sem0",
			.reader	= 0,
			.writer	= 64,
		},
		{
			.name	= "64 readers and 64 writers",
			.dev	= "sem0",
			.reader	= 64,
			.writer	= 64,
		},
		{
			.name	= "256 readers and 16 writers",
			.dev	= "sem0",
			.reader	= 256,
			.writer	= 16,
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
			tester(t);
		ret = waitpid(pid, &status, 0);
		if (ret == -1)
			goto perr;
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: returns failed exit\n", t->name);
			goto err;
		}
		ksft_inc_pass_cnt();
		continue;
perr:
		fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
