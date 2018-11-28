/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>

#include "ldd.h"
#include "scull.h"
#include "sleepy.h"
#include "sculld.h"

static int ldd_bus_match(struct device *dev, struct device_driver *drv)
{
	size_t len = strlen(drv->name);
	const char *dname = dev_name(dev);
	char *suffix;

	/* device name prefix should be the driver name */
	if (strncmp(dname, drv->name, len))
		return 0;
	/* device name is same with the driver name */
	if (strlen(dname) == len)
		return 1;
	/* device name suffix only contains digit, or ":" */
	for (suffix = (char *)&dname[len]; *suffix; suffix++)
		if ((int)*suffix < 0x30 || (int)*suffix > 0x3A)
			return 0;
	return 1;
}

/* ldd_bus_type is the top level virtual bus which hosts
 * all the ldd devices. */
struct bus_type ldd_bus_type = {
	.name	= "ldd",
	.match	= ldd_bus_match,
};

static int __init ldd_init(void)
{
	int err;

	printk(KERN_INFO "Welcome to the wonderful kernel world!\n");
	err = bus_register(&ldd_bus_type);
	if (err)
		goto error;
	err = scull_register();
	if (err)
		goto error_scull;
	err = sleepy_register();
	if (err)
		goto error_sleepy;
	err = sculld_register();
	if (err)
		goto error_sculld;
	return 0;
error_sculld:
	sleepy_unregister();
error_sleepy:
	scull_unregister();
error_scull:
	bus_unregister(&ldd_bus_type);
error:
	return err;
}
module_init(ldd_init);

static void __exit ldd_exit(void)
{
	sculld_unregister();
	sleepy_unregister();
	scull_unregister();
	bus_unregister(&ldd_bus_type);
	printk(KERN_INFO "Have a wonderful day!\n");
}
module_exit(ldd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Noguchi <mail@keinoguchi.com>");
MODULE_DESCRIPTION("Linux Device Driver in Action");
