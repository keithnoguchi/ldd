/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

struct write_device {
	struct mutex	lock;
	size_t		size;
	struct cdev	cdev;
	struct device	base;
};

static struct write_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct write_device	devs[1000]; /* 1000 devices!? */
} write_driver = {
	.base.name	= "write",
	.base.owner	= THIS_MODULE,
};

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct write_device *dev = fp->private_data;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (dev->size < *pos+count)
		dev->size = *pos+count;
	mutex_unlock(&dev->lock);
	*pos += count;
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct write_device *dev = container_of(ip->i_cdev, struct write_device, cdev);
	fp->private_data = dev;
	if ((fp->f_flags&O_ACCMODE) == O_RDONLY)
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (fp->f_flags&O_TRUNC)
		dev->size = 0;
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct write_device *dev = container_of(base, struct write_device, base);
	size_t size;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	size = dev->size;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", size);
}
static DEVICE_ATTR_RO(size);

static void __init init_driver(struct write_driver *drv)
{
	drv->fops.write	= write;
	drv->fops.open	= open;
}

static int __init init(void)
{
	struct write_driver *drv = &write_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct write_device *dev;
	char name[9]; /* for 1000 devices */
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, nr, drv->base.name);
	if (err)
		return err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
		memset(dev, 0, sizeof(struct write_device));
		mutex_init(&dev->lock);
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		dev->base.init_name = name;
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			j = i;
			goto err;
		}
		err = device_create_file(&dev->base, &dev_attr_size);
		if (err) {
			j = i+1;
			goto err;
		}
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++) {
		device_remove_file(&dev->base, &dev_attr_size);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, nr);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct write_driver *drv = &write_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct write_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		device_remove_file(&dev->base, &dev_attr_size);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("write(2) example");
