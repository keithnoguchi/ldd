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
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	int		flags;
	int		nr;
	char		mark[5];
	size_t		size;
	size_t		want;
	size_t		alloc;
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
	char *ptr, obuf[t->size], ibuf[t->size];
	char path[PATH_MAX];
	ssize_t len, rem, got;
	int i, err, fd;
	FILE *fp;

	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	ptr = obuf;
	for (i = 0; i < t->size/sizeof(t->mark); i++) {
		memcpy(ptr, t->mark, sizeof(t->mark));
		ptr += sizeof(t->mark);
	}
	for (i = 0; i < t->size%sizeof(t->mark); i++)
		ptr[i] = t->mark[i];
	for (i = 0; i < t->nr; i++) {
		int flags = t->flags|O_WRONLY;

		fd = open(path, i == 0 ? flags|O_TRUNC : flags);
		if (fd == -1)
			goto perr;
		rem = t->size;
		ptr = obuf;
		do {
			len = write(fd, ptr, rem);
			if (len == -1) {
				fprintf(stderr, "%s: write: %s\n",
					t->name, strerror(errno));
				goto err;
			}
			rem -= len;
			ptr += len;
		} while (rem > 0);
		if (close(fd) == -1)
			goto perr;
	}
	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto perr;
	rem = sizeof(ibuf);
	ptr = ibuf;
	got = 0;
	while ((len = read(fd, ptr, rem)) > 0) {
		got += len;
		rem -= len;
		ptr += len;
		if (rem == 0) {
			if (memcmp(ibuf, obuf, sizeof(ibuf))) {
				fprintf(stderr, "%s: unexpected receive data\n",
					t->name);
				dump(stderr, "ibuf", (unsigned char *)ibuf, sizeof(ibuf));
				dump(stderr, "obuf", (unsigned char *)obuf, sizeof(ibuf));
				goto err;
			}
			rem = sizeof(ibuf);
			ptr = ibuf;
		}
	}
	if (close(fd) == -1)
		goto perr;
	if (got != t->want) {
		fprintf(stderr, "%s: unexpected read result:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->want, got);
		goto err;
	}
	err = snprintf(path, sizeof(path), "/sys/devices/%s/size", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(ibuf, sizeof(ibuf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(ibuf, NULL, 10);
	if (got != t->want) {
		fprintf(stderr, "%s: unexpected %s value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, path, t->want, got);
		goto err;
	}
	err = snprintf(path, sizeof(path), "/sys/devices/%s/alloc", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(ibuf, sizeof(ibuf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	got = strtol(ibuf, NULL, 10);
	if (got != t->alloc) {
		fprintf(stderr, "%s: unexpected %s value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, path, t->alloc, got);
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
	long pagesize = sysconf(_SC_PAGESIZE);
	const struct test *t, tests[] = {
		{
			.name	= "signle 4095 write no O_APPEND flag",
			.dev	= "append0",
			.flags	= 0,
			.mark	= {0x01, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4095,
			.want	= 4095,
			.alloc	= (4095/pagesize+1)*pagesize,
		},
		{
			.name	= "single 4095 write with O_APPEND flag",
			.dev	= "append1",
			.flags	= O_APPEND,
			.mark	= {0x02, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4095,
			.want	= 4095,
			.alloc	= (4095/pagesize+1)*pagesize,
		},
		{
			.name	= "signle 4096 write no O_APPEND flag",
			.dev	= "append2",
			.flags	= 0,
			.mark	= {0x03, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4096,
			.want	= 4096,
			.alloc	= (4096/pagesize+1)*pagesize,
		},
		{
			.name	= "single 4096 write with O_APPEND flag",
			.dev	= "append3",
			.flags	= O_APPEND,
			.mark	= {0x04, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4096,
			.want	= 4096,
			.alloc	= (4096/pagesize+1)*pagesize,
		},
		{
			.name	= "signle 4097 write no O_APPEND flag",
			.dev	= "append0",
			.flags	= 0,
			.mark	= {0x05, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4097,
			.want	= 4097,
			.alloc	= (4097/pagesize+1)*pagesize,
		},
		{
			.name	= "single 4097 write with O_APPEND flag",
			.dev	= "append1",
			.flags	= O_APPEND,
			.mark	= {0x06, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 1,
			.size	= 4097,
			.want	= 4097,
			.alloc	= (4097/pagesize+1)*pagesize,
		},
		{
			.name	= "double 4095 writes no O_APPEND flag",
			.dev	= "append2",
			.flags	= 0,
			.mark	= {0x07, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4095,
			.want	= 4095,
			.alloc	= (4095/pagesize+1)*pagesize,
		},
		{
			.name	= "double 4095 writes with O_APPEND flag",
			.dev	= "append3",
			.flags	= O_APPEND,
			.mark	= {0x08, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4095,
			.want	= 8190,
			.alloc	= (8190/pagesize+1)*pagesize,
		},
		{
			.name	= "double 4096 writes no O_APPEND flag",
			.dev	= "append0",
			.flags	= 0,
			.mark	= {0x09, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4096,
			.want	= 4096,
			.alloc	= (4096/pagesize+1)*pagesize,
		},
		{
			.name	= "double 4096 writes with O_APPEND flag",
			.dev	= "append1",
			.flags	= O_APPEND,
			.mark	= {0x0a, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4096,
			.want	= 8192,
			.alloc	= (8192/pagesize)*pagesize,
		},
		{
			.name	= "double 4097 writes no O_APPEND flag",
			.dev	= "append2",
			.flags	= 0,
			.mark	= {0x0b, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4097,
			.want	= 4097,
			.alloc	= (4097/pagesize+1)*pagesize,
		},
		{
			.name	= "double 4097 writes with O_APPEND flag",
			.dev	= "append3",
			.flags	= O_APPEND,
			.mark	= {0x0c, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 2,
			.size	= 4097,
			.want	= 8194,
			.alloc	= (8194/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4095 writes no O_APPEND flag",
			.dev	= "append0",
			.flags	= 0,
			.mark	= {0x0d, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4095,
			.want	= 4095,
			.alloc	= (4095/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4095 writes with O_APPEND flag",
			.dev	= "append1",
			.flags	= O_APPEND,
			.mark	= {0x0e, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4095,
			.want	= 12285,
			.alloc	= (12285/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4096 writes no O_APPEND flag",
			.dev	= "append2",
			.flags	= 0,
			.mark	= {0x0f, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4096,
			.want	= 4096,
			.alloc	= (4096/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4096 writes with O_APPEND flag",
			.dev	= "append3",
			.flags	= O_APPEND,
			.mark	= {0x10, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4096,
			.want	= 12288,
			.alloc	= (12288/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4097 writes no O_APPEND flag",
			.dev	= "append0",
			.flags	= 0,
			.mark	= {0x11, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4097,
			.want	= 4097,
			.alloc	= (4097/pagesize+1)*pagesize,
		},
		{
			.name	= "triple 4097 writes with O_APPEND flag",
			.dev	= "append1",
			.flags	= O_APPEND,
			.mark	= {0x12, 0xde, 0xad, 0xbe, 0xef},
			.nr	= 3,
			.size	= 4097,
			.want	= 12291,
			.alloc	= (12291/pagesize+1)*pagesize,
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
