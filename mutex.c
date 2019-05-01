/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

struct mutex_device {
	struct miscdevice	base;
};

static struct mutex_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct mutex_device	devs[1];
} mutex_driver = {
	.base.name	= "mutex",
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

static void __init init_driver(struct mutex_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct mutex_driver *drv = &mutex_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct mutex_device *dev;
	char name[7];
	int err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
		memset(dev, 0, sizeof(struct mutex_device));
		dev->base.name	= name;
		dev->base.fops	= &drv->fops;
		dev->base.minor	= MISC_DYNAMIC_MINOR;
		err = misc_register(&dev->base);
		if (err) {
			j = i;
			goto err;
		}
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct mutex_driver *drv = &mutex_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct mutex_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Mutex example driver");
