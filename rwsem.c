/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/rwsem.h>

struct rwsem_device {
	struct rw_semaphore	lock;
	struct miscdevice	base;
};

static struct rwsem_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct rwsem_device	devs[1];
} rwsem_driver = {
	.base.name	= "rwsem",
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

static void __init init_driver(struct rwsem_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct rwsem_driver *drv = &rwsem_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct rwsem_device *dev;
	char name[7]; /* sizeof(drv->base.name)+2 */
	int err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%i",
			       drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
		init_rwsem(&dev->lock);
		memset(&dev->base, 0, sizeof(struct miscdevice));
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
	struct rwsem_driver *drv = &rwsem_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct rwsem_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/rwsem.h> example");
