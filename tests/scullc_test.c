/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	size_t		len;
	size_t		qset_size;
	size_t		quantum_size;
	size_t		qset_count;
};

static void test(const struct test *restrict t)
{
	char buf[t->len > 80 ? t->len : 80];
	char path[PATH_MAX];
	int ret, fd;
	long got;
	FILE *fp;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/qset_size",
		       t->dev);
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
	if (got != t->qset_size) {
		fprintf(stderr, "%s: unexpected qset size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->qset_size, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/quantum_size",
		       t->dev);
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
	if (got != t->quantum_size) {
		fprintf(stderr, "%s: unexpected quantum size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->quantum_size, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		goto perr;
	ret = write(fd, buf, t->len);
	if (ret == -1)
		goto perr;
	if (ret != t->len) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n\t-  got: %d\n",
			t->name, t->len, ret);
		goto err;
	}
	if (close(fd) == -1)
		goto perr;
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/qset_count", t->dev);
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
	if (got != t->qset_count) {
		fprintf(stderr, "%s: unexpected qset count:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->qset_count, got);
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
			.name		= "write 1024 bytes to scullc0",
			.dev		= "scullc0",
			.len		= 1024,
			.qset_size	= 511,
			.quantum_size	= 4096,
			.qset_count	= (1024+1)/(511*4096)+1,
		},
		{
			.name		= "write 2048 bytes to scullc1",
			.dev		= "scullc1",
			.len		= 2048,
			.qset_size	= 511,
			.quantum_size	= 4096,
			.qset_count	= (1024+1)/(511*4096)+1,
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
