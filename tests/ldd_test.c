/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include "kselftest.h"

static int open_ldd_bus_file()
{
	int fd;

	fd = open("/sys/bus/ldd", O_RDONLY);
	if (fd == -1) {
		perror("open_ldd_bus_file");
		ksft_inc_fail_cnt();
		return 1;
	}
	close(fd);
	ksft_inc_pass_cnt();
	return 0;
}

int main(void)
{
	int fail = 0;

	fail += open_ldd_bus_file();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
