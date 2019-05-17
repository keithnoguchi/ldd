/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

struct seqlock_device {
	struct miscdevice	base;
};

static struct seqlock_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct seqlock_device	devs[2];
} seqlock_driver = {
	.base.name	= "seqlock",
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

static int __init init_driver(struct seqlock_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct seqlock_driver *drv = &seqlock_driver;
	struct seqlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seqlock_device *dev;
	char name[9]; /* strlen(drv->base.name)+2 */
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
		memset(dev, 0, sizeof(struct seqlock_device));
		dev->base.name	= name;
		dev->base.fops	= &drv->fops;
		dev->base.minor	= MISC_DYNAMIC_MINOR;
		err = misc_register(&dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct seqlock_driver *drv = &seqlock_driver;
	struct seqlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seqlock_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Sequence lock test module");
