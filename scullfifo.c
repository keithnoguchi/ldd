/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

struct scullfifo_device {
	struct cdev	cdev;
	struct device	base;
};

static struct scullfifo_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct scullfifo_device	devs[2];
} scullfifo_driver = {
	.base.name	= "scullfifo",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	return 0;
}

static int __init init_driver(struct scullfifo_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct scullfifo_driver *drv = &scullfifo_driver;
	struct scullfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullfifo_device *dev;
	char name[11]; /* strlen(drv->base.name)+2 */
	int err, i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct scullfifo_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		dev->base.init_name	= name;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scullfifo_driver *drv = &scullfifo_driver;
	struct scullfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullfifo_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguhi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull fifo device driver");

