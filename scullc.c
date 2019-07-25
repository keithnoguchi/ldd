/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>

struct scullc_device {
	struct cdev	cdev;
	struct device	base;
};

static struct scullc_driver {
	dev_t			devt;
	struct device_driver	base;
	struct file_operations	fops;
	struct scullc_device	devs[1];
} scullc_driver = {
	.base.name	= "scullc",
	.base.owner	= THIS_MODULE,
};

static ssize_t write(struct file *fp, const char *__user buf, size_t count, loff_t *pos)
{
	*pos += count;
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scullc_device *dev = container_of(ip->i_cdev,
						 struct scullc_device,
						 cdev);
	printk(KERN_DEBUG "open[%s:%s]\n", dev->base.driver->name,
	       dev_name(&dev->base));
	return 0;
}

static int init_driver(struct scullc_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.write	= write;
	drv->fops.open	= open;
	return 0;
}

static int __init init(void)
{
	struct scullc_driver *drv = &scullc_driver;
	struct scullc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullc_device *dev;
	char name[8]; /* strlen(drv->name)+2 */
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
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->base.init_name	= name;
		dev->base.driver	= &drv->base;
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
	struct scullc_driver *drv = &scullc_driver;
	struct scullc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullc_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull backed by lookaside cache");
