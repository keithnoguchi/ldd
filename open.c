/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/sysfs.h>

struct open_device {
	atomic_t	open_nr;
	struct cdev	cdev;
	struct device	base;
};

static struct open_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct open_device	devs[1000]; /* 1000 devices!? */
} open_driver = {
	.base.owner	= THIS_MODULE,
	.base.name	= "open",
};

static int open(struct inode *ip, struct file *fp)
{
	struct open_device *dev = container_of(ip->i_cdev, struct open_device, cdev);
	fp->private_data = dev;
	atomic_inc(&dev->open_nr);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct open_device *dev = fp->private_data;
	atomic_dec(&dev->open_nr);
	return 0;
}

static ssize_t open_nr_show(struct device *dev, struct device_attribute *attr,
			    char *page)
{
	struct open_device *d = container_of(dev, struct open_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&d->open_nr));
}
static DEVICE_ATTR_RO(open_nr);

static void __init init_driver(struct open_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct open_driver *drv = &open_driver;
	struct open_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct open_device *dev;
	char name[8]; /* for 1000 devices */
	int i, err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err <= 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct open_device));
		atomic_set(&dev->open_nr, 0);
		cdev_init(&dev->cdev, &drv->fops);
		dev->cdev.owner	= THIS_MODULE;
		device_initialize(&dev->base);
		dev->base.init_name = name;
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
		err = device_create_file(&dev->base, &dev_attr_open_nr);
		if (err) {
			end = dev+1;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev < end; dev++) {
		device_remove_file(&dev->base, &dev_attr_open_nr);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct open_driver *drv = &open_driver;
	struct open_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct open_device *dev;

	for (dev = drv->devs; dev < end; dev++) {
		device_remove_file(&dev->base, &dev_attr_open_nr);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("open(2) and close(2) example module");
