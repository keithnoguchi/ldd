/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/fcntl.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uio.h>

struct writev_device {
	struct mutex	lock;
	size_t		size;
	struct cdev	cdev;
	struct device	base;
};

static struct writev_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct writev_device	devs[4]; /* 4 device nodes */
} writev_driver = {
	.base.name	= "writev",
	.base.owner	= THIS_MODULE,
};

static ssize_t write_iter(struct kiocb *cb, struct iov_iter *iter)
{
	struct writev_device *dev = cb->ki_filp->private_data;
	const struct iovec *iov;
	size_t offset;
	ssize_t total;
	int i;

	if (!iter_is_iovec(iter))
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	offset = iter->iov_offset;
	total = 0;
	for (i = 0, iov = iter->iov; i < iter->nr_segs; i++, iov++) {
		total += iov->iov_len;
		offset += iov->iov_len;
		if (offset > dev->size)
			dev->size = offset;
	}
	mutex_unlock(&dev->lock);
	return total;
}

static int open(struct inode *ip, struct file *fp)
{
	struct writev_device *dev = container_of(ip->i_cdev, struct writev_device, cdev);

	if ((fp->f_flags&O_ACCMODE) == O_RDONLY)
		return -EINVAL;
	fp->private_data = dev;
	if (!(fp->f_flags&O_TRUNC))
		goto out;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	dev->size = 0;
	mutex_unlock(&dev->lock);
out:
	return 0;
}

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct writev_device *dev = container_of(base, struct writev_device, base);
	ssize_t size;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	size = dev->size;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", size);
}
static DEVICE_ATTR_RO(size);

static void __init init_driver(struct writev_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.write_iter	= write_iter;
	drv->fops.open		= open;
}

static int __init init(void)
{
	struct writev_driver *drv = &writev_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct writev_device *dev;
	char name[8]; /* 10 devices */
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
		memset(dev, 0, sizeof(struct writev_device));
		dev->size = 0;
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
	struct writev_driver *drv = &writev_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct writev_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		device_remove_file(&dev->base, &dev_attr_size);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("writev(2) example character driver");
