/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include "ldd.h"

/* Scull driver */
static struct device_driver driver = {
	.owner	= THIS_MODULE,
	.name	= "scull",
};

/* Scull devices */
static struct scull_device {
	struct device	dev;
	struct cdev	cdev;
} devices[] = {
	{
		.dev.init_name	= "scull0",
		.dev.release	= ldd_release_device,
	},
	{
		.dev.init_name	= "scull1",
		.dev.release	= ldd_release_device,
	},
	{
		.dev.init_name	= "scull2:1",
		.dev.release	= ldd_release_device,
	},
	{	/* Dummy device */
		.dev.init_name	= "scullX",
		.dev.release	= ldd_release_device,
	},
	{},	/* sentry */
};

static int scull_open(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);
	struct device_driver *drv;

	drv = d->dev.driver;
	if (drv != &driver)
		return -ENODEV;
	f->private_data = d;
	return 0;
}

static ssize_t scull_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct scull_device *d = f->private_data;
	printk(KERN_INFO "read(%s:%s)\n", d->dev.driver->name, dev_name(&d->dev));
	return  0;
}

static ssize_t scull_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	*pos += len;
	return len;
}

static int scull_release(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);
	struct device_driver *drv;

	drv = d->dev.driver;
	if (drv != &driver)
		return -ENODEV;
	return 0;
}

static const struct file_operations scull_fops = {
	.open		= scull_open,
	.read		= scull_read,
	.write		= scull_write,
	.release	= scull_release,
};

int scull_register(void)
{
	struct scull_device *dev, *dev_err = NULL;
	dev_t devt;
	int err;
	int i;

	/* scull driver */
	err = ldd_register_driver(&driver);
	if (err)
		return err;

	/* scull devices */
	err = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(devices), driver.name);
	if (err)
		goto out;
	i = 0;
	for (dev = &devices[0]; dev->dev.init_name; dev++) {
		dev->dev.devt = MKDEV(MAJOR(devt), MINOR(devt)+(i++));
		err = ldd_register_device(&dev->dev);
		if (err) {
			dev_err = --dev;
			goto out;
		}
		cdev_init(&dev->cdev, &scull_fops);
		cdev_set_parent(&dev->cdev, &dev->dev.kobj);
		err = cdev_add(&dev->cdev, dev->dev.devt, 1);
		if (err) {
			dev_err = dev;
			goto out;
		}
	}
	return 0;
out:
	if (dev_err)
		for (dev = &devices[0]; dev != dev_err; dev++) {
			cdev_del(&dev->cdev);
			ldd_unregister_device(&dev->dev);
		}
	if (MAJOR(devices[0].dev.devt))
		unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
	return err;
}

void scull_unregister(void)
{
	struct scull_device *dev;

	for (dev = &devices[0]; dev_name(&dev->dev); dev++) {
		cdev_del(&dev->cdev);
		ldd_unregister_device(&dev->dev);
	}
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
}
