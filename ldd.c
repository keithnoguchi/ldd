/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>

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

int ldd_register_device(struct device *dev)
{
	dev->bus = &ldd_bus_type;
	return device_register(dev);
}
EXPORT_SYMBOL(ldd_register_device);

void ldd_unregister_device(struct device *dev)
{
	device_unregister(dev);
}
EXPORT_SYMBOL(ldd_unregister_device);

void ldd_release_device(struct device *dev)
{
}
EXPORT_SYMBOL(ldd_release_device);

int ldd_register_driver(struct device_driver *drv)
{
	drv->bus = &ldd_bus_type;
	return driver_register(drv);
}
EXPORT_SYMBOL(ldd_register_driver);

void ldd_unregister_driver(struct device_driver *drv)
{
	driver_unregister(drv);
}
EXPORT_SYMBOL(ldd_unregister_driver);

static int __init init(void)
{
	int err;
	err = bus_register(&ldd_bus_type);
	if (err)
		return err;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	bus_unregister(&ldd_bus_type);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Noguchi <mail@keinoguchi.com>");
MODULE_DESCRIPTION("Linux Device Driver in Action");
