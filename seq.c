/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>

static int __init init(void)
{
	printk(KERN_ALERT "here you go!\n");
	return 0;
}
module_init(init);

static void __exit term(void)
{
	printk(KERN_ALERT "goodbye\n");
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/seq_file.h> example");
