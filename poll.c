/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>

struct poll_device {
	struct cdev	cdev;
	struct device	base;
};

static struct poll_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct poll_device	devs[3];
} poll_driver = {
	.base.name	= "poll",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	return 0;
}

static __poll_t poll(struct file *fp, struct poll_table_struct *wait)
{
	return 0;
}

static int open(struct inode *ip, struct file *fp)
{
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	return 0;
}

static int __init init_driver(struct poll_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.poll		= poll;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct poll_driver *drv = &poll_driver;
	struct poll_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct poll_device *dev;
	char name[6]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct poll_device));
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
	struct poll_driver *drv = &poll_driver;
	struct poll_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct poll_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("select(2), poll(2), epoll(7) test driver");
