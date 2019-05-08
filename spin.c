/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

struct spin_device {
	struct miscdevice	base;
};

static struct spin_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct spin_device	devs[2];
} spin_driver = {
	.base.name	= "spin",
	.base.owner	= THIS_MODULE,
};

static int __init init_driver(struct spin_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	return 0;
}

static int __init init(void)
{
	struct spin_driver *drv = &spin_driver;
	int err;

	err = init_driver(drv);
	if (err)
		return err;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	return;
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Spin lock test module");
