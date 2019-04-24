/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include "kselftest.h"

int main(void)
{
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
