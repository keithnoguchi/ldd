/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>

/* Scull driver */
static struct device_driver driver = {
	.name	= "scull",
};

/* Scull devices */
static struct scull_device {
	struct device	dev;
	struct cdev	cdev;
} devices[] = {
	{
		.dev.init_name	= "scull0",
	},
	{}, /* sentry */
};

static int scull_open(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);
	printk("open(%s)\n", dev_name(&d->dev));
	return 0;
}

static ssize_t scull_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	return 0;
}

static int scull_release(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);
	printk("release(%s)\n", dev_name(&d->dev));
	return 0;
}

static const struct file_operations scull_fops = {
	.open		= scull_open,
	.read		= scull_read,
	.release	= scull_release,
};

int scull_register(void)
{
	struct scull_device *d, *d_err = NULL;
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
		device_initialize(&d->dev);
		cdev_init(&d->cdev, &scull_fops);
		err = cdev_device_add(&d->cdev, &d->dev);
		if (err) {
			d_err = d;
			goto out;
		}
	}
	return 0;
out:
	for (d = devices; d != d_err; d++)
		cdev_device_del(&d->cdev, &d->dev);
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	return err;
}

void scull_unregister(void)
{
	struct scull_device *d;
	for (d = devices; dev_name(&d->dev); d++)
		cdev_device_del(&d->cdev, &d->dev);
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
}
