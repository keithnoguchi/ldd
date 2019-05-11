/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	size_t		qset;
	size_t		quantum;
	size_t		size;
	char		mark[4];
};

static void dump(FILE *s, const char *tag, const unsigned char *restrict buf, size_t len)
{
	int i, j, width = 16;

	fprintf(s, "%s> ", tag);
	for (i = 0; i < len; i++) {
		fprintf(s, "%02x ", buf[i]);
		/* check if we are done for this line */
		if (i%width == (width-1) || i+1 == len) {
			/* fill the gap */
			for (j = (i%width); j < (width-1); j++)
				fprintf(s, "   ");
			/* ascii print */
			fprintf(s, "| ");
			for (j = i-(i%width); j <= i; j++)
				fprintf(s, "%c", isprint(buf[j]) ? buf[j] : '.');
			fprintf(s, "\n%s> ", tag);
		}
	}
	fprintf(s, "\n");
}

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char obuf[BUFSIZ];
	char ibuf[BUFSIZ];
	size_t marksize;
	int ret;
	FILE *fp;
	long got;

	/* quantum set */
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/qset", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "w");
	if (fp == NULL)
		goto perr;
	ret = snprintf(obuf, sizeof(obuf), "%ld\n", t->qset);
	if (ret < 0)
		goto perr;
	ret = fwrite(obuf, sizeof(obuf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(ibuf, sizeof(ibuf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(ibuf, NULL, 10);
	if (got != t->qset) {
		fprintf(stderr, "%s: unexpected qset value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->qset, got);
		goto err;
	}
	fclose(fp);
	/* quantum */
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/quantum", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "w");
	if (fp == NULL)
		goto perr;
	ret = snprintf(obuf, sizeof(obuf), "%ld\n", t->quantum);
	if (ret < 0)
		goto perr;
	ret = fwrite(obuf, sizeof(obuf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(ibuf, sizeof(ibuf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(ibuf, NULL, 10);
	if (got != t->quantum) {
		fprintf(stderr, "%s: unexpected quantum size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->quantum, got);
		goto err;
	}
	fclose(fp);
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	/* write */
	if ((t->flags&O_ACCMODE) != O_RDONLY) {
		ssize_t len, rem;
		int i, fd;

		fd = open(path, t->flags);
		if (fd == -1)
			goto perr;
		marksize = sizeof(t->mark);
		for (i = 0; i < sizeof(obuf)/marksize; i++)
			memcpy(obuf+(marksize*i), t->mark, marksize);
		for (rem = t->size; rem; rem -= len) {
			void *ptr = obuf;
			ssize_t n, r;

			n = len = rem < sizeof(obuf) ? rem : sizeof(obuf);
write:
			r = write(fd, ptr, n);
			if (r == -1)
				goto perr;
			else if (r == n)
				continue;
			ptr += r;
			n -= r;
			goto write;
		}
		if (close(fd))
			goto perr;
	}
	/* read */
	if ((t->flags&O_ACCMODE) != O_WRONLY) {
		ssize_t len, rem;
		int fd;

		fd = open(path, O_RDONLY);
		if (fd == -1)
			goto perr;
		for (rem = t->size; rem; rem -= len) {
			void *ptr = ibuf;
			ssize_t n, r;

			n = len = rem < sizeof(ibuf) ? rem : sizeof(ibuf);
read:
			r = read(fd, ptr, n);
			if (r == -1)
				goto perr;
			else if (r == 0) {
				if (memcmp(ibuf, obuf, len)) {
					fprintf(stderr, "%s: unexpected buffer\n",
						t->name);
					dump(stderr, "obuf", (unsigned char *)obuf, len);
					dump(stderr, "ibuf", (unsigned char *)ibuf, len);
					goto err;
				}
				continue;
			}
			ptr += r;
			n -= r;
			goto read;
		}
		if (close(fd))
			goto perr;
	}
	/* size */
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	ret = fread(ibuf, sizeof(ibuf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(ibuf, NULL, 10);
	if (got != t->size) {
		fprintf(stderr, "%s: unexpected size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->size, got);
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
			.name		= "scull0 read only open",
			.dev		= "scull0",
			.flags		= O_RDONLY,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 0,
		},
		{
			.name		= "scull1 write only open",
			.dev		= "scull1",
			.flags		= O_WRONLY,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 0,
		},
		{
			.name		= "scull2 read/write open",
			.dev		= "scull2",
			.flags		= O_RDWR,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 0,
		},
		{
			.name		= "scull3 read/write trunc open",
			.dev		= "scull3",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 0,
		},
		{
			.name		= "scull0 1024 O_TRUNC write",
			.dev		= "scull0",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 1024,
		},
		{
			.name		= "scull1 4095 O_TRUNC write",
			.dev		= "scull0",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4095,
		},
		{
			.name		= "scull2 4096 O_TRUNC write",
			.dev		= "scull2",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4096,
		},
		{
			.name		= "scull3 4097 O_TRUNC write",
			.dev		= "scull3",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4097,
		},
		{
			.name		= "scull0 1024 O_TRUNC write on (1/32)",
			.dev		= "scull0",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 1024,
		},
		{
			.name		= "scull1 4095 O_TRUNC write on (1/32)",
			.dev		= "scull0",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4095,
		},
		{
			.name		= "scull2 4096 O_TRUNC write on (1/32)",
			.dev		= "scull2",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4096,
		},
		{
			.name		= "scull3 4097 O_TRUNC write on (1/32)",
			.dev		= "scull3",
			.flags		= O_WRONLY|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4097,
		},
		{
			.name		= "scull0 4 O_TRUNC write/read",
			.dev		= "scull0",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4,
			.mark		= {0x0e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull0 1024 O_TRUNC write/read",
			.dev		= "scull0",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 1024,
			.mark		= {0x1e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull1 4095 O_TRUNC write/read",
			.dev		= "scull0",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4095,
			.mark		= {0x2e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull2 4096 O_TRUNC write/read",
			.dev		= "scull2",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4096,
			.mark		= {0x3e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull3 4097 O_TRUNC write/read",
			.dev		= "scull3",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1024,
			.quantum	= 4096,
			.size		= 4097,
			.mark		= {0x4e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull0 1024 O_TRUNC write/read on (1/32)",
			.dev		= "scull0",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 1024,
			.mark		= {0x5e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull1 4095 O_TRUNC write/read on (1/32)",
			.dev		= "scull0",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4095,
			.mark		= {0x6e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull2 4096 O_TRUNC write/read on (1/32)",
			.dev		= "scull2",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4096,
			.mark		= {0x7e, 0xad, 0xbe, 0xef},
		},
		{
			.name		= "scull3 4097 O_TRUNC write/read on (1/32)",
			.dev		= "scull3",
			.flags		= O_RDWR|O_TRUNC,
			.qset		= 1,
			.quantum	= 32,
			.size		= 4097,
			.mark		= {0x8e, 0xad, 0xbe, 0xef},
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
