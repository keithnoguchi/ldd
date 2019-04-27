/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

struct faulty_device {
	struct miscdevice	base;
};

static struct faulty_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct faulty_device	devs[2];
} faulty_driver = {
	.base.owner	= THIS_MODULE,
	.base.name	= "faulty",
};

static void __init init_driver(struct faulty_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
}

static int __init init(void)
{
	struct faulty_driver *drv = &faulty_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct faulty_device *dev;
	char name[8]; /* sizeof(drv->base.name)+2 */
	int err;

	init_driver(drv);
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
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct faulty_driver *drv = &faulty_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct faulty_device *dev;
	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Oops example");
