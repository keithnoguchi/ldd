/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/uio.h>
#include <asm/page.h>

struct readv_device {
	struct mutex	lock;
	size_t		size;
	struct cdev	cdev;
	struct device	base;
};

struct readv_driver {
	dev_t			devt;
	size_t			default_size;
	struct file_operations	fops;
	struct device_driver	base;
	struct readv_device	devs[4];
} readv_driver = {
	.base.name	= "readv",
	.base.owner	= THIS_MODULE,
	.default_size	= PAGE_SIZE,
};
module_param_named(default_size, readv_driver.default_size, ulong, 0444);

static ssize_t read_iter(struct kiocb *cb, struct iov_iter *iter)
{
	struct readv_device *dev = cb->ki_filp->private_data;
	size_t offset, count;
	ssize_t total;
	int i;

	if (!iter_is_iovec(iter))
		return -EINVAL;
	offset = iter->iov_offset;
	total = iter->count;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (offset+total > dev->size)
		total = dev->size-offset;
	if (total == 0)
		goto out;
	/* go through the vector */
	for (i = 0; i < iter->nr_segs; i++) {
		count = iter->iov[i].iov_len;
		if (offset+count > dev->size)
			count = dev->size-offset;
		offset += count;
	}
out:
	mutex_unlock(&dev->lock);
	return total;
}

static int open(struct inode *ip, struct file *fp)
{
	struct readv_device *dev = container_of(ip->i_cdev, struct readv_device, cdev);
	fp->private_data = dev;

	if ((fp->f_flags&O_ACCMODE) == O_WRONLY)
		return -EINVAL;
	return 0;
}

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct readv_device *dev = container_of(base, struct readv_device, base);
	size_t size;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	size = dev->size;
	mutex_unlock(&dev->lock);
	return size;
}

static ssize_t size_store(struct device *base, struct device_attribute *attr,
			  const char *page, size_t count)
{
	struct readv_device *dev = container_of(base, struct readv_device, base);
	size_t size;
	int err;

	err = kstrtol(page, 10, &size);
	if (err)
		return err;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	dev->size = size;
	mutex_unlock(&dev->lock);
	return count;
}
static DEVICE_ATTR_RW(size);

static void __init init_driver(struct readv_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.read_iter	= read_iter;
	drv->fops.open		= open;
}

static int __init init(void)
{
	struct readv_driver *drv = &readv_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	char name[7]; /* 10 devices */
	struct readv_device *dev;
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
		memset(dev, 0, sizeof(struct readv_device));
		dev->size = drv->default_size;
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
			j = i++;
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
	struct readv_driver *drv = &readv_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct readv_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		device_remove_file(&dev->base, &dev_attr_size);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("readv(2) example");
