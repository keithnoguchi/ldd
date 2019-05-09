/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

struct faulty_device {
	bool			crash;
	struct miscdevice	base;
};

static struct faulty_driver {
	bool			crash;
	struct file_operations	fops;
	struct device_driver	base;
	struct faulty_device	devs[2];
} faulty_driver = {
	.crash		= false,	/* call dump_stack() by default */
	.base.name	= "faulty",
	.base.owner	= THIS_MODULE,
};
module_param_named(crash, faulty_driver.crash, bool, S_IWUSR|S_IRUGO);

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct faulty_device *dev = container_of(fp->private_data,
						 struct faulty_device, base);
	if (dev->crash)
		*(int *)0 = 0; /* let it crash */
	dump_stack();
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct faulty_device *dev = container_of(fp->private_data,
						 struct faulty_device, base);
	if (dev->crash)
		*(int *)0 = 0;
	dump_stack();
	return 0;
}

static void __init init_driver(struct faulty_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.read	= read;
	drv->fops.write	= write;
}

static int __init init(void)
{
	struct faulty_driver *drv = &faulty_driver;
	struct faulty_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct faulty_device *dev;
	char name[8]; /* strlen(drv->base.name)+2 */
	int i, err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct faulty_device));
		dev->crash	= drv->crash;
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
	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct faulty_driver *drv = &faulty_driver;
	struct faulty_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct faulty_device *dev;

	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Oops test module");
