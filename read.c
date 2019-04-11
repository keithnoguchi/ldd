/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

struct read_device {
	size_t		size;
	struct cdev	cdev;
	struct device	base;
};

static struct read_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct read_device	devs[1000]; /* 1000 devices!? */
} read_driver = {
	.base.name	= "read",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct read_device *dev = fp->private_data;
	return 0;
}

static int open(struct inode *ip, struct file *fp)
{
	struct read_device *dev = container_of(ip->cdev, struct read_device, cdev);
	fp->private_data = dev;
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct read_device *dev = fp->private_data;
	fp->private_data = NULL;
	return 0;
}

static void __init init_driver(struct read_driver *drv)
{
	drv->fops.read		= read;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct read_driver *drv = &read_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct read_device *dev;
	char name[8]; /* for 1000 devices */
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, nr, drv->base.name);
	if (err)
		return err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err <= 0) {
			j = i;
			goto err;
		}
		dev->base.init_name = name;
		dev->base.driver = &drv->base;
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		dev->size = 0;
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			j = i;
			goto err;
		}
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct read_driver *drv = &read_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct read_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("read(2) example");
