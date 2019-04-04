/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
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
	struct device	dev;
	struct cdev	cdev;
} devices[] = {
	{
		.dev.init_name	= "sculld0",
		.dev.release	= ldd_release_device,
	},
	{
		.dev.init_name	= "sculld1",
		.dev.release	= ldd_release_device,
	},
	{
		.dev.init_name	= "sculld2:1",
		.dev.release	= ldd_release_device,
	},
	{	/* Dummy device */
		.dev.init_name	= "sculldX",
		.dev.release	= ldd_release_device,
	},
	{},	/* sentry */
};

/* Sculld device attributes */
static ssize_t size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sculld_device *d = container_of(dev, struct sculld_device, dev);
	return snprintf(buf, PAGE_SIZE, "%ld\n", d->size);
}
static DEVICE_ATTR_RO(size);

static ssize_t bufsiz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sculld_device *d = container_of(dev, struct sculld_device, dev);
	return snprintf(buf, PAGE_SIZE, "%ld\n", d->bufsiz);
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
	struct sculld_device *d = container_of(i->i_cdev, struct sculld_device, cdev);
	struct device_driver *drv;

	drv = d->dev.driver;
	if (drv != &driver)
		return -ENODEV;
	f->private_data = d;
	/* truncate the device size if it's write only or truncated */
	if (f->f_flags & O_WRONLY || f->f_flags & O_TRUNC)
		d->size = 0;
	return 0;
}

static ssize_t sculld_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct sculld_device *d = f->private_data;
	printk(KERN_INFO "read(%s:%s)\n", d->dev.driver->name, dev_name(&d->dev));
	return  0;
}

static ssize_t sculld_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	struct sculld_device *d = f->private_data;

	/* naive buffer management */
	if (*pos+len > d->bufsiz) {
		if (d->buf)
			kfree(d->buf);
		d->bufsiz = 0;
		d->buf = kmalloc(*pos+len, GFP_KERNEL);
		if (IS_ERR(d->buf))
			return PTR_ERR(d->buf);
		d->bufsiz = *pos+len;
	}
	*pos += len;
	if (*pos > d->size)
		d->size = *pos;
	return len;
}

static int sculld_release(struct inode *i, struct file *f)
{
	struct sculld_device *d = container_of(i->i_cdev, struct sculld_device, cdev);
	struct device_driver *drv;

	drv = d->dev.driver;
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
	for (dev = &devices[0]; dev->dev.init_name; dev++) {
		dev->dev.type = &sculld_device_type;
		dev->dev.devt = MKDEV(MAJOR(devt), MINOR(devt)+(i++));
		err = ldd_register_device(&dev->dev);
		if (err) {
			dev_err = --dev;
			goto err;
		}
		cdev_init(&dev->cdev, &sculld_fops);
		cdev_set_parent(&dev->cdev, &dev->dev.kobj);
		err = cdev_add(&dev->cdev, dev->dev.devt, 1);
		if (err) {
			dev_err = dev;
			goto err;
		}
	}
	return 0;
err:
	if (dev_err)
		for (dev = &devices[0]; dev != dev_err; dev++) {
			cdev_del(&dev->cdev);
			ldd_unregister_device(&dev->dev);
		}
	if (MAJOR(devices[0].dev.devt))
		unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct sculld_device *dev;

	for (dev = &devices[0]; dev_name(&dev->dev); dev++) {
		if (dev->buf)
			kfree(dev->buf);
		cdev_del(&dev->cdev);
		ldd_unregister_device(&dev->dev);
	}
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	ldd_unregister_driver(&driver);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("SCULL under LDD bus");
