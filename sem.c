/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/semaphore.h>

struct sem_device {
	struct miscdevice	base;
};

static struct sem_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct sem_device	devs[1];
} sem_driver = {
	.base.name	= "sem",
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

static int __init init_driver(struct sem_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct sem_driver *drv = &sem_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct sem_device *dev;
	char name[5]; /* sizeof(drv->base.name)+2 */
	int err;

	err = init_driver(drv);
	if (err)
		return err;
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
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
	for (i = 0, dev = drv->devs; i < j; i++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct sem_driver *drv = &sem_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct sem_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, drv++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/semaphore.h> example driver");
