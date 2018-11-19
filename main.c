/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>

static int ldd_init(void)
{
	printk(KERN_INFO "Welcome to the wonderful kernel world!\n");
	return 0;
}
module_init(ldd_init);

static void ldd_exit(void)
{
	printk(KERN_INFO "Have a wonderful day!\n");
}
module_exit(ldd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Noguchi <mail@keinoguchi.com>");
