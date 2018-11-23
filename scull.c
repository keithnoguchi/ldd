/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/device.h>
#include "ldd.h"

/* Scull devices */
static struct device devices[] = {
	{
		.init_name	= "scull0",
		.release	= ldd_release_device,
	},
	{
		.init_name	= "scull1",
		.release	= ldd_release_device,
	},
	{	/* Dummy device */
		.init_name	= "scullX",
		.release	= ldd_release_device,
	},
	{},	/* sentry */
};

/* Scull driver */
static struct device_driver driver = {
	.name	= "scull",
};

int scull_register(void)
{
	struct device *dev, *dev_err = NULL;
	int err;

	for (dev = &devices[0]; dev->init_name; dev++) {
		err = ldd_register_device(dev);
		if (err) {
			dev_err = dev;
			goto out;
		}
	}
	err = ldd_register_driver(&driver);
	if (err)
		dev_err = devices;
out:
	if (dev_err)
		for (dev = &devices[0]; dev != dev_err; dev++)
			ldd_unregister_device(dev);
	return err;
}

void scull_unregister(void)
{
	struct device *dev;

	ldd_unregister_driver(&driver);
	for (dev = &devices[0]; dev_name(dev); dev++)
		ldd_unregister_device(dev);
}
