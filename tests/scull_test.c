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
	size_t		qset;
	size_t		quantum;
	size_t		size;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[BUFSIZ];
	int ret, fd;
	FILE *fp;
	long got;

	/* quantum set */
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/qset", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "w");
	if (fp == NULL)
		goto perr;
	ret = snprintf(buf, sizeof(buf), "%ld\n", t->qset);
	if (ret < 0)
		goto perr;
	ret = fwrite(buf, sizeof(buf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(buf, NULL, 10);
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
	ret = snprintf(buf, sizeof(buf), "%ld\n", t->quantum);
	if (ret < 0)
		goto perr;
	ret = fwrite(buf, sizeof(buf), 1, fp);
	if (ret == -1)
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != t->quantum) {
		fprintf(stderr, "%s: unexpected quantum size:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->quantum, got);
		goto err;
	}
	fclose(fp);
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	/* device operation */
	fd = open(path, t->flags);
	if (fd == -1)
		goto perr;
	/* write */
	if ((t->flags&O_ACCMODE) != O_RDONLY) {
		ssize_t len, rem;
		for (rem = t->size; rem; rem -= len) {
			len = rem < sizeof(buf) ? rem : sizeof(buf);
			len = write(fd, buf, len);
			if (len == -1)
				goto perr;
		}
	}
	if (close(fd))
		goto perr;
	/* size */
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/size", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	got = strtol(buf, NULL, 10);
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

static int test_open(const char *path, int flags)
{
	int fd = open(path, flags);
	if (fd == -1)
		return errno;
	close(fd);
	return 0;
}

static int test_attr_getl(const char *path, long *get)
{
	char buf[BUFSIZ];
	int err = 0;
	FILE *fp;
	long got;

	fp = fopen(path, "r");
	if (fp == NULL)
		return errno;
	err = fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp)) {
		err = errno;
		goto out;
	}
	got = strtol(buf, NULL, 10);
	if (got <= LONG_MIN || got >= LONG_MAX) {
		err = EINVAL;
		goto out;
	}
	err = 0;
	*get = got;
out:
	fclose(fp);
	return err;
}

static int test_attr_readl(const char *path, long want)
{
	long got;
	int err;

	err = test_attr_getl(path, &got);
	if (err)
		return err;
	if (got != want) {
		fprintf(stderr, "got(%ld)!=want(%ld)\n", got, want);
		return EINVAL;
	}
	return 0;
}

static ssize_t test_readn(const char *path, size_t len, int n, off_t offset,
			  size_t *got)
{
	char *buf = NULL;
	size_t total;
	int err;
	int fd;
	int i;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return errno;

	buf = malloc(len);
	if (buf == NULL) {
		err = errno;
		goto out;
	}
	if (offset)
		if (lseek(fd, offset, SEEK_SET) == -1) {
			err = errno;
			goto out;
		}
	total = 0;
	for (i = 0; i < n; i++) {
		int pos = 0;
		while (pos < len) {
			int ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				err = errno;
				goto out;
			}
			/* treat the zero read as success */
			if (ret == 0)
				break;
			pos += ret;
		}
		total += pos;
	}
	*got = total;
	err = 0;
out:
	if (buf)
		free(buf);
	close(fd);
	return err;
}

static int test_writen(const char *path, size_t len, int nr, off_t offset)
{
	char *buf = NULL;
	int err;
	int fd;
	int i;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return errno;

	buf = malloc(len);
	if (buf == NULL) {
		err = errno;
		goto out;
	}
	if (offset)
		if (lseek(fd, offset, SEEK_SET) == -1) {
			err = errno;
			goto out;
		}
	for (i = 0; i < nr; i++) {
		int pos = 0;
		while (pos < len) {
			int ret = write(fd, buf+pos, len-pos);
			if (ret == -1) {
				err = errno;
				goto out;
			}
			pos += ret;
		}
	}
	err = 0;
out:
	if (buf)
		free(buf);
	close(fd);
	return err;
}

static int test_writen_and_readn(const char *path, size_t len, int nr)
{
	char *wbuf = NULL, *rbuf = NULL;
	int wlen, rlen;
	int ret;
	int fd;
	int i;

	/* Reset the device by opening it write only */
	ret = test_open(path, O_WRONLY);
	if (ret)
		return ret;

	/* prepare the buffer */
	wbuf = malloc(len*nr);
	if (wbuf == NULL)
		return errno;
	memset(wbuf, 0xaa, len*nr);

	rbuf = malloc(len*nr);
	if (rbuf == NULL) {
		ret = errno;
		free(wbuf);
		return ret;
	}
	memset(rbuf, 0xbb, len*nr);

	/* Open file read and write */
	fd = open(path, O_RDWR);
	if (fd == -1)
		goto out;

	/* first write it. */
	wlen = 0;
	for (i = 0; i < nr; i++) {
		char *buf = &wbuf[len*i];
		int pos = 0;
		while (pos < len) {
			int ret = write(fd, buf+pos, len-pos);
			if (ret == -1) {
				ret = errno;
				goto out;
			}
			wlen += ret;
			pos += ret;
		}
	}

	/* Read file from the beginning */
	ret = lseek(fd, 0, SEEK_SET);
	if (ret == -1) {
		ret = errno;
		goto out;
	}

	rlen = 0;
	for (i = 0; i < nr; i++) {
		char *buf = &rbuf[len*i];
		int pos = 0;
		while (pos < len) {
			ret = read(fd, buf+pos, len-pos);
			if (ret == -1) {
				ret = errno;
				goto out;
			}
			if (ret == 0)
				break;
			rlen += ret;
			pos += ret;
		}
	}
	if (rlen != wlen) {
		ret = EINVAL;
		goto out;
	}
	if (rlen != len*nr) {
		ret = EINVAL;
		goto out;
	}
	if (memcmp(rbuf, wbuf, len*nr)) {
		for (i = 0; i < len*nr; i++)
			if (rbuf[i] != wbuf[i]) {
				fprintf(stderr, "rbuf[%d](0x%hhx) != wbuf[%d](0x%hhx)\n",
					i, rbuf[i], i, wbuf[i]);
				break;
			}
		ret = EINVAL;
		goto out;
	}
	ret = 0;
out:
	if (rbuf)
		free(rbuf);
	if (wbuf)
		free(wbuf);
	if (fd != -1)
		close(fd);
	return ret;
}

void test_scull_attr_readl(void)
{
	const struct test {
		const char	*name;
		const char	*path;
		long		want;
	} *t, tests[] = {
		{
			.name	= "read scull0's buffer size",
			.path	= "/sys/devices/scull0/buffer/size",
			.want	= 0,
		},
		{},	/* sentry */
	};

	for (t = tests; t->name; t++) {
		int err = test_attr_readl(t->path, t->want);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		ksft_inc_pass_cnt();
	}
}

void test_scull_readn(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
		off_t		offset;
	} *t, tests[] = {
		{
			.name	= "read 0 byte from scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 0,
			.offset	= 0,
		},
		{
			.name	= "read 1 byte from scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "read 4096 bytes from scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "read 16KiB(4KiB x 4) from scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 4,
			.offset	= 0,
		},
		{
			.name	= "read 1 byte on 4KiB offset from scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 4096,
		},
		{}, /* sentry */
	};
	char path[BUFSIZ];
	size_t total;

	for (t = tests; t->name; t++) {
		int err = sprintf(path, "/dev/%s", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		total = 0;
		err = test_readn(path, t->len, t->count, t->offset, &total);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/size", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = test_attr_readl(path, total);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		ksft_inc_pass_cnt();
	}
}

void test_scull_writen(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
		off_t		offset;
	} *t, tests[] = {
		{
			.name	= "write 0 byte on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1 byte on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 4KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 4097 bytes on scull0",
			.dev	= "scull0",
			.len	= 4097,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 8KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 2,
			.offset	= 0,
		},
		{
			.name	= "write 0 byte on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
			.offset	= 0,
		},
		{
			.name	= "write 1 byte with 4KiB offset on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
			.offset	= 4096,
		},
		{},	/* sentry */
	};

	for (t = tests; t->name; t++) {
		char path[BUFSIZ];
		long quantum, bufsize;
		int err;

		err = sprintf(path, "/dev/%s", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = test_writen(path, t->len, t->count, t->offset);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/size", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = test_attr_readl(path, t->len*t->count+t->offset);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/quantum", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = test_attr_getl(path, &quantum);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = sprintf(path, "/sys/devices/%s/buffer/size", t->dev);
		if (err == -1) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		bufsize = 0;
		if ((t->len*t->count) != 0) {
			bufsize = ((t->len*t->count)/quantum)*quantum;
			if ((t->len*t->count)%quantum)
				bufsize += quantum;
		}
		err = test_attr_readl(path, bufsize);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		ksft_inc_pass_cnt();
	}
}

void test_scull_writen_and_readn(void)
{
	const struct test {
		const char	*name;
		const char	*dev;
		size_t		len;
		int		count;
	} *t, tests[] = {
		{
			.name	= "write and read 0 bytes on scull0",
			.dev	= "scull0",
			.len	= 0,
			.count	= 1,
		},
		{
			.name	= "write and read 1 bytes on scull0",
			.dev	= "scull0",
			.len	= 1,
			.count	= 1,
		},
		{
			.name	= "write and read 1KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 1,
		},
		{
			.name	= "write and read 4KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 1,
		},
		{
			.name	= "write and read 4KiB on scull0",
			.dev	= "scull0",
			.len	= 1024,
			.count	= 4,
		},
		{
			.name	= "write and read 4097 bytes on scull0",
			.dev	= "scull0",
			.len	= 4097,
			.count	= 1,
		},
		{
			.name	= "write and read 8KiB on scull0",
			.dev	= "scull0",
			.len	= 4096,
			.count	= 2,
		},
		{},	/* sentry */
	};

	for (t = tests; t->name; t++) {
		char path[BUFSIZ];
		int err;

		err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
		if (err == -1) {
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		err = test_writen_and_readn(path, t->len, t->count);
		if (err) {
			errno = err;
			perror(t->name);
			ksft_inc_fail_cnt();
			continue;
		}
		ksft_inc_pass_cnt();
	}
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
#if 0
	test_scull_attr_readl();
	test_scull_readn();
	test_scull_writen();
	test_scull_writen_and_readn();
#endif
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
