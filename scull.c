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
#include <linux/uaccess.h>

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
	size_t ssize = sizeof(struct scull_qset)+sizeof(void *)*dev->qset;
	struct scull_qset *newp, **datap = &dev->data;
	int i, spos = pos/(dev->quantum*dev->qset);

	for (i = 0; i < spos; i++) {
		if (*datap) {
			datap = &(*datap)->next;
			continue;
		}
		newp = kmalloc(ssize, GFP_KERNEL);
		if (!newp)
			return NULL;
		memset(newp, 0, ssize);
		*datap = newp;
		datap = &newp->next;
	}
	if (*datap)
		return *datap;
	newp = kmalloc(ssize, GFP_KERNEL);
	if (!newp)
		return NULL;
	memset(newp, 0, ssize);
	*datap = newp;
	return newp;
}

static void scull_trim(struct scull_device *dev)
{
	struct scull_qset *data, *next;
	int i;

	for (data = dev->data; data; data = next) {
		for (i = 0; i < dev->qset; i++)
			if (data->data[i])
				kfree(data->data[i]);
		next = data->next;
		kfree(data);
	}
	dev->data = NULL;
	dev->size = 0;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct scull_device *dev = fp->private_data;
	struct scull_qset *qset;
	ssize_t ret = -ENOMEM;
	size_t spos, qpos, dpos;
	size_t rem, len;
	void **data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (*pos > dev->size) {
		ret = 0;
		goto out;
	}
	qset = scull_follow(dev, *pos);
	if (!qset)
		goto out;
	spos = *pos%(dev->qset*dev->quantum);
	qpos = spos/dev->quantum;
	dpos = spos%dev->quantum;
	data = &qset->data[qpos];
	if (!*data) {
		*data = kmalloc(dev->quantum, GFP_KERNEL);
		if (!*data)
			goto out;
		memset(*data, 0, dev->quantum);
	}
	if (dpos+count > dev->quantum)
		count = dev->quantum-dpos;
	len = count;
	while ((rem = copy_to_user(buf, *data+dpos, len))) {
		dpos += len-rem;
		buf += len-rem;
		len = rem;
	}
	*pos += count;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct scull_device *dev = fp->private_data;
	struct scull_qset *qset;
	ssize_t ret = -ENOMEM;
	size_t spos, qpos, dpos;
	size_t rem, len;
	void **data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	qset = scull_follow(dev, *pos);
	if (!qset)
		goto out;
	spos = *pos%(dev->qset*dev->quantum);
	qpos = spos/dev->quantum;
	dpos = spos%dev->quantum;
	data = &qset->data[qpos];
	if (!*data) {
		*data = kmalloc(dev->quantum, GFP_KERNEL);
		if (!*data)
			goto out;
		memset(*data, 0, dev->quantum);
	}
	/* per quantum write */
	if (count > dev->quantum-dpos)
		count = dev->quantum-dpos;
	/* copy_from_user() does not return error */
	len = count;
	while ((rem = copy_from_user(*data+dpos, buf, len))) {
		dpos += len-rem;
		buf += len-rem;
		len = rem;
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
	drv->fops.owner		= drv->base.owner;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.open		= open;
}

static int __init init(void)
{
	struct scull_driver *drv = &scull_driver;
	struct scull_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scull_device *dev;
	char name[7]; /* 10 devices */
	int i, err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct scull_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		dev->data		= NULL;
		dev->size		= 0;
		dev->qset		= drv->default_qset;
		dev->quantum		= drv->default_quantum;
		dev->cdev.owner		= drv->base.owner;
		dev->base.init_name	= name;
		dev->base.type		= &drv->type;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev < end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scull_driver *drv = &scull_driver;
	struct scull_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scull_device *dev;

	for (dev = drv->devs; dev < end; dev++) {
		scull_trim(dev);
		cdev_device_del(&dev->cdev, &dev->base);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Simple Character Utility for Loading Localities");
