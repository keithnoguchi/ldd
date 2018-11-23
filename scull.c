/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/device.h>
#include "ldd.h"

/* Scull devices. */
static struct device devices[] = {
	{
		.init_name	= "scull0",
		.release	= ldd_release_device,
	},
	{
		.init_name	= "scull1",
		.release	= ldd_release_device,
	},
	{}, /* sentry */
};

int scull_register(void)
{
	struct device *dev, *dev_err = NULL;
	int err;

	printk(KERN_INFO "Let's add scull devices\n");
	for (dev = &devices[0]; dev->init_name; dev++) {
		err = ldd_register_device(dev);
		if (err) {
			dev_err = dev;
			goto out;
		}
	}
out:
	if (dev_err)
		for (dev = &devices[0]; dev != dev_err; dev++)
			ldd_unregister_device(dev);
	return err;
}

void scull_unregister(void)
{
	struct device *dev;

	printk(KERN_INFO "Let's cleanup scull devices\n");
	for (dev = &devices[0]; dev_name(dev); dev++)
		ldd_unregister_device(dev);
}
