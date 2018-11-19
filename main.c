/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>

/* ldd_bus_type is the top level virtual bus which hosts
 * all the ldd devices. */
static struct bus_type ldd_bus_type = {
	.name		= "ldd",
};

static int ldd_init(void)
{
	int err;

	printk(KERN_INFO "Welcome to the wonderful kernel world!\n");
	err = bus_register(&ldd_bus_type);
	if (err < 0)
		return err;
	return 0;
}
module_init(ldd_init);

static void ldd_exit(void)
{
	printk(KERN_INFO "Have a wonderful day!\n");
	bus_unregister(&ldd_bus_type);
}
module_exit(ldd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Noguchi <mail@keinoguchi.com>");
