/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	int		err;
	size_t		total;
	size_t		each;
	size_t		want;
};

static void test(const struct test *restrict t)
{
	char *ptr, buf[t->total > LINE_MAX ? t->total : LINE_MAX];
	FILE *fp = NULL;
	int ret, fd = -1;
	size_t remain;
	long val;

	ret = snprintf(buf, sizeof(buf), "/dev/%s", t->dev);
	if (ret < 0)
		goto err;
	fd = open(buf, t->flags);
	if (fd == -1)
		goto err;
	remain = t->total;
	ptr = buf;
	while (remain > 0) {
		ssize_t nr = write(fd, ptr, t->each);
		if (nr == -1)
			goto err;
		remain -= nr;
		ptr += nr;
	}
	ret = snprintf(buf, sizeof(buf), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto err;
	fp = fopen(buf, "r");
	if (fp == NULL)
		goto err;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp)) {
		errno = EINVAL;
		goto err;
	}
	val = strtol(buf, NULL, 10);
	if (val < 0 || val > LONG_MAX) {
		errno = EINVAL;
		goto err;
	}
	if (val != t->want) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->want, val);
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
err:
	if (t->err && errno == t->err)
		exit(EXIT_SUCCESS);
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
	if (fd != -1)
		close(fd);
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name	= "open read only",
			.dev	= "write0",
			.flags	= O_RDONLY,
			.err	= EINVAL,
		},
		{
			.name	= "open write only",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 0,
			.want	= 0,
		},
		{
			.name	= "open read-write",
			.dev	= "write0",
			.flags	= O_RDWR|O_TRUNC,
			.total	= 0,
			.want	= 0,
		},
		{
			.name	= "write 1024 with 1 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 1024,
			.each	= 1,
			.want	= 1024,
		},
		{
			.name	= "write 1024 with 2 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 1024,
			.each	= 2,
			.want	= 1024,
		},
		{
			.name	= "write 1024 with 512 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 1024,
			.each	= 512,
			.want	= 1024,
		},
		{
			.name	= "write 1024 with 1024 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 1024,
			.each	= 1024,
			.want	= 1024,
		},
		{
			.name	= "write 4096 with 1 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 4096,
			.each	= 1,
			.want	= 4096,
		},
		{
			.name	= "write 4096 with 1024 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 4096,
			.each	= 1024,
			.want	= 4096,
		},
		{
			.name	= "write 4096 with 4096 byte each",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 4096,
			.each	= 4096,
			.want	= 4096,
		},
		{
			.name	= "continuation from the previous test with 1 byte write",
			.dev	= "write0",
			.flags	= O_WRONLY,
			.total	= 1,
			.each	= 1,
			.want	= 4096,
		},
		{
			.name	= "truncation of the previous write",
			.dev	= "write0",
			.flags	= O_WRONLY|O_TRUNC,
			.total	= 1,
			.each	= 1,
			.want	= 1,
		},
		{.name = NULL},
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto err;
		} else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1) {
			fprintf(stderr, "%s: %s\n",
				t->name, strerror(errno));
			goto err;
		}
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: signaled with %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: not exited\n",
				t->name);
			goto err;
		}
		if (WEXITSTATUS(status))
			goto err;
		ksft_inc_pass_cnt();
		continue;
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
