/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>

/* Scull driver */
static struct device_driver driver = {
	.name	= "scull",
};

/* Scull devices */
static struct scull_device {
	struct device	dev;
} devices[] = {
	{
		.dev.init_name	= "scull0",
	},
	{}, /* sentry */
};

int scull_register(void)
{
	struct scull_device *d;
	dev_t devt;
	int err;
	int i;

	/* allocate scull device region */
	err = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(devices), driver.name);
	if (err)
		return err;
	/* create devices */
	i = 0;
	for (d = devices; d->dev.init_name; d++) {
		d->dev.driver = &driver;
		d->dev.devt = MKDEV(MAJOR(devt), MINOR(devt)+i++);
	}
	return 0;
}

void scull_unregister(void)
{
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	return;
}
