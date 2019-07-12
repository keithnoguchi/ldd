/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>

struct lseek_device {
	size_t		alloc;
	struct cdev	cdev;
	struct device	base;
};

static struct lseek_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct lseek_device	devs[4];
} lseek_driver = {
	.base.name		= "lseek",
	.base.owner		= THIS_MODULE,
	.devs[0]	= {
		.alloc		= 16,
		.base.init_name	= "lseek16",
	},
	.devs[1]	= {
		.alloc		= 64,
		.base.init_name	= "lseek64",
	},
	.devs[2]	= {
		.alloc		= 128,
		.base.init_name	= "lseek128",
	},
	.devs[3]	= {
		.alloc		= 256,
		.base.init_name	= "lseek256",
	},
};

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct lseek_device *dev = container_of(base, struct lseek_device,
						base);
	return snprintf(page, PAGE_SIZE, "%ld\n", dev->alloc);
}
static DEVICE_ATTR_RO(alloc);

static struct attribute *lseek_attrs[] = {
	&dev_attr_alloc.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lseek);

static int init_driver(struct lseek_driver *drv)
{
	int size = ARRAY_SIZE(drv->devs);
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, size, drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	return 0;
}

static int __init init(void)
{
	struct lseek_driver *drv = &lseek_driver;
	struct lseek_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct lseek_device *dev;
	int err;
	int i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		device_initialize(&dev->base);
		dev->base.groups	= lseek_groups;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		cdev_init(&dev->cdev, &drv->fops);
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
	struct lseek_driver *drv = &lseek_driver;
	struct lseek_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct lseek_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("lseek(2) test module");
