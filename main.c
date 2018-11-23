/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>

#include "ldd.h"
#include "scull.h"

/* ldd_bus_type is the top level virtual bus which hosts
 * all the ldd devices. */
struct bus_type ldd_bus_type = {
	.name		= "ldd",
};

static int ldd_init(void)
{
	int err;

	printk(KERN_INFO "Welcome to the wonderful kernel world!\n");
	err = bus_register(&ldd_bus_type);
	if (err < 0)
		goto out;
	err = scull_register();
	if (err < 0)
		goto out_bus;
	return 0;
out_bus:
	bus_unregister(&ldd_bus_type);
out:
	return err;
}
module_init(ldd_init);

static void ldd_exit(void)
{
	scull_unregister();
	bus_unregister(&ldd_bus_type);
	printk(KERN_INFO "Have a wonderful day!\n");
}
module_exit(ldd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Noguchi <mail@keinoguchi.com>");
