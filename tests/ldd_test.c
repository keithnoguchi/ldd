/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include "kselftest.h"

int main(void)
{
	int fail = 0;
	if (fail)
		ksft_inc_fail_cnt();
	else
		ksft_inc_pass_cnt();
	if (fail)
		ksft_exit_fail();
	ksft_exit_pass();
}
