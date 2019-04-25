/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>

#include "ldd.h"

/* Sculld driver */
static struct device_driver driver = {
	.owner	= THIS_MODULE,
	.name	= "sculld",
};

/* Sculld devices */
static struct sculld_device {
	size_t		size;
	char		*buf;
	size_t		bufsiz;
	struct cdev	cdev;
	struct device	base;
} devices[] = {
	{
		.base.init_name	= "sculld0",
		.base.release	= ldd_release_device,
	},
	{
		.base.init_name	= "sculld1",
		.base.release	= ldd_release_device,
	},
	{
		.base.init_name	= "sculld2:1",
		.base.release	= ldd_release_device,
	},
	{	/* Dummy device */
		.base.init_name	= "sculldX",
		.base.release	= ldd_release_device,
	},
	{},	/* sentry */
};

/* Sculld device attributes */
static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *buf)
{
	struct sculld_device *dev = container_of(base, struct sculld_device, base);
	return snprintf(buf, PAGE_SIZE, "%ld\n", dev->size);
}
static DEVICE_ATTR_RO(size);

static ssize_t bufsiz_show(struct device *base, struct device_attribute *attr,
			   char *buf)
{
	struct sculld_device *dev = container_of(base, struct sculld_device, base);
	return snprintf(buf, PAGE_SIZE, "%ld\n", dev->bufsiz);
}
static DEVICE_ATTR_RO(bufsiz);

/* Sculld attribute groups */
static struct attribute *sculld_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_bufsiz.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sculld);

/* Scull device type */
static struct device_type sculld_device_type = {
	.name	= "sculld",
	.groups	= sculld_groups,
};

static int sculld_open(struct inode *i, struct file *f)
{
	struct sculld_device *dev = container_of(i->i_cdev, struct sculld_device, cdev);
	struct device_driver *drv;

	drv = dev->base.driver;
	if (drv != &driver)
		return -ENODEV;
	f->private_data = dev;
	/* truncate the device size if it's write only or truncated */
	if (f->f_flags & O_WRONLY || f->f_flags & O_TRUNC)
		dev->size = 0;
	return 0;
}

static ssize_t sculld_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct sculld_device *dev = f->private_data;
	printk(KERN_INFO "read(%s:%s)\n", dev->base.driver->name,
	       dev_name(&dev->base));
	return  0;
}

static ssize_t sculld_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	struct sculld_device *dev = f->private_data;

	/* naive buffer management */
	if (*pos+len > dev->bufsiz) {
		if (dev->buf)
			kfree(dev->buf);
		dev->bufsiz = 0;
		dev->buf = kmalloc(*pos+len, GFP_KERNEL);
		if (IS_ERR(dev->buf))
			return PTR_ERR(dev->buf);
		dev->bufsiz = *pos+len;
	}
	*pos += len;
	if (*pos > dev->size)
		dev->size = *pos;
	return len;
}

static int sculld_release(struct inode *i, struct file *f)
{
	struct sculld_device *dev = f->private_data;
	struct device_driver *drv;

	drv = dev->base.driver;
	if (drv != &driver)
		return -ENODEV;
	return 0;
}

static const struct file_operations sculld_fops = {
	.open		= sculld_open,
	.read		= sculld_read,
	.write		= sculld_write,
	.release	= sculld_release,
};

static int __init init(void)
{
	struct sculld_device *dev, *dev_err = NULL;
	dev_t devt;
	int err;
	int i;

	/* sculld driver */
	err = ldd_register_driver(&driver);
	if (err)
		return err;

	/* sculld devices */
	err = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(devices), driver.name);
	if (err)
		goto err;
	i = 0;
	for (dev = devices; dev->base.init_name; dev++) {
		cdev_init(&dev->cdev, &sculld_fops);
		dev->cdev.owner = THIS_MODULE;
		dev->base.type = &sculld_device_type;
		dev->base.devt = MKDEV(MAJOR(devt), MINOR(devt)+(i++));
		err = ldd_register_device(&dev->base);
		if (err) {
			dev_err = --dev;
			goto err;
		}
		cdev_set_parent(&dev->cdev, &dev->base.kobj);
		err = cdev_add(&dev->cdev, dev->base.devt, 1);
		if (err) {
			dev_err = dev;
			goto err;
		}
	}
	return 0;
err:
	if (dev_err)
		for (dev = devices; dev != dev_err; dev++) {
			cdev_del(&dev->cdev);
			ldd_unregister_device(&dev->base);
		}
	if (MAJOR(devices[0].base.devt))
		unregister_chrdev_region(devices[0].base.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct sculld_device *dev;

	for (dev = devices; dev_name(&dev->base); dev++) {
		if (dev->buf)
			kfree(dev->buf);
		cdev_del(&dev->cdev);
		ldd_unregister_device(&dev->base);
	}
	unregister_chrdev_region(devices[0].base.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull on LDD bus");
