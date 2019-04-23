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
#include <linux/slab.h>

struct scull_qset {
	struct scull_qset	*next;
	void			*data[];
};

struct scull_device {
	struct mutex		lock;
	struct scull_qset	*data;
	size_t			qset;
	size_t			quantum;
	size_t			size;
	struct cdev		cdev;
	struct device		base;
};

static struct scull_driver {
	dev_t			devt;
	size_t			default_qset;
	size_t			default_quantum;
	struct file_operations	fops;
	struct device_type	type;
	struct device_driver	base;
	struct scull_device	devs[4];
} scull_driver = {
	.default_qset		= 1024,
	.default_quantum	= PAGE_SIZE,
	.base.name		= "scull",
	.base.owner		= THIS_MODULE,
};

static struct scull_qset *scull_follow(struct scull_device *dev, loff_t pos)
{
	struct scull_qset **datap, *newp;
	int i, spos = pos/(dev->quantum*dev->qset);

	datap = &dev->data;
	for (i = 0; i < spos; i++) {
		if (*datap) {
			datap = &(*datap)->next;
			continue;
		}
		newp = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (!newp)
			return NULL;
		memset(newp, 0, sizeof(struct scull_qset));
		*datap = newp;
		datap = &newp->next;
	}
	if (*datap)
		return *datap;
	newp = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
	if (!newp)
		return NULL;
	memset(newp, 0, sizeof(struct scull_qset));
	*datap = newp;
	return newp;
}

static void scull_trim(struct scull_device *dev)
{
	struct scull_qset *data, *next;
	for (data = dev->data; data; data = next) {
		next = data->next;
		kfree(data);
	}
	dev->data = NULL;
	dev->size = 0;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct scull_device *dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (*pos+count > dev->size)
		count = dev->size-*pos;
	mutex_unlock(&dev->lock);
	if (count < 0)
		count = 0;
	*pos += count;
	return count;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct scull_device *dev = fp->private_data;
	struct scull_qset *qset;
	ssize_t ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	qset = scull_follow(dev, *pos);
	if (!qset) {
		ret = -ENOMEM;
		goto out;
	}
	if (dev->size < *pos+count)
		dev->size = *pos+count;
	*pos += count;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scull_device *dev = container_of(ip->i_cdev, struct scull_device, cdev);
	fp->private_data = dev;
	if ((fp->f_flags&O_ACCMODE) == O_RDONLY)
		return 0;
	if (!(fp->f_flags&O_TRUNC))
		return 0;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	scull_trim(dev);
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t qset_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct scull_device *dev = container_of(base, struct scull_device, base);
	size_t qset;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	qset = dev->qset;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", qset);
}

static ssize_t qset_store(struct device *base, struct device_attribute *attr,
			  const char *page, size_t count)
{
	struct scull_device *dev = container_of(base, struct scull_device, base);
	long qset;
	int err;

	err = kstrtol(page, 10, &qset);
	if (err)
		return err;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (qset == dev->qset)
		goto out;
	scull_trim(dev);
	dev->qset = qset;
out:
	mutex_unlock(&dev->lock);
	return count;
}
static DEVICE_ATTR_RW(qset);

static ssize_t quantum_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scull_device *dev = container_of(base, struct scull_device, base);
	size_t quantum;
	if (mutex_lock_interruptible(&dev->lock))
	    return -ERESTARTSYS;
	quantum = dev->quantum;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", quantum);
}

static ssize_t quantum_store(struct device *base, struct device_attribute *attr,
			     const char *page, size_t count)
{
	struct scull_device *dev = container_of(base, struct scull_device, base);
	long quantum;
	int err;

	err = kstrtol(page, 10, &quantum);
	if (err)
		return err;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (quantum == dev->quantum)
		goto out;
	scull_trim(dev);
	dev->quantum = quantum;
out:
	mutex_unlock(&dev->lock);
	return count;
}
static DEVICE_ATTR_RW(quantum);

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct scull_device *dev = container_of(base, struct scull_device, base);
	size_t size;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	size = dev->size;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", size);
}
static DEVICE_ATTR_RO(size);

static struct attribute *top_attrs[] = {
	&dev_attr_qset.attr,
	&dev_attr_quantum.attr,
	&dev_attr_size.attr,
	NULL,
};
ATTRIBUTE_GROUPS(top);

static void __init init_driver(struct scull_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->type.name		= drv->base.name;
	drv->type.groups	= top_groups;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.open		= open;
}

static int __init init(void)
{
	struct scull_driver *drv = &scull_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct scull_device *dev;
	char name[7]; /* 10 devices */
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
		memset(dev, 0, sizeof(struct scull_device));
		mutex_init(&dev->lock);
		dev->qset = drv->default_qset;
		dev->quantum = drv->default_quantum;
		dev->data = NULL;
		dev->size = 0;
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		dev->base.init_name = name;
		dev->base.type = &drv->type;
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			j = i;
			goto err;
		}
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, nr);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scull_driver *drv = &scull_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct scull_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		scull_trim(dev);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Simple Character Utility for Loading Localities");
