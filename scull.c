/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>

int scull_register(void)
{
	printk(KERN_INFO "Let's add scull devices\n");
	return 0;
}

void scull_unregister(void)
{
	printk(KERN_INFO "Let's cleanup the scull devices\n");
}
