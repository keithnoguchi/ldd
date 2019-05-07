/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>

struct append_device {
	struct mutex	lock;
	size_t		size;
	size_t		alloc;
	void		*data;
	struct cdev	cdev;
	struct device	base;
};

static struct append_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct append_device	devs[4]; /* four devices */
} append_driver = {
	.base.name	= "append",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct append_device *dev = fp->private_data;
	char *data = dev->data;
	ssize_t len;

	printk(KERN_DEBUG "[%s:%d] read(buf=%p,count=%ld,*pos=%lld)\n",
	       dev_name(&dev->base), task_pid_nr(current), buf, count, *pos);
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	len = 0;
	if (count == 0 || *pos >= dev->size)
		goto out;
	if (*pos+count > dev->size)
		count = dev->size-*pos;
	data += *pos;
	len = count;
	do {
		unsigned long rem = copy_to_user(buf, data, len);
		if (!rem)
			break;
		data += len-rem;
		buf += len-rem;
		len = rem;
	} while (len);
	*pos += count;
	len = count;
out:
	mutex_unlock(&dev->lock);
	return len;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct append_device *dev = fp->private_data;
	char *data;
	loff_t lpos;
	size_t len;

	printk(KERN_DEBUG "[%s:%d] write(buf=%p,count=%ld,*pos=%lld)\n",
	       dev_name(&dev->base), task_pid_nr(current), buf, count, *pos);
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	len = 0;
	if (count == 0)
		goto out;
	lpos = *pos;
	if (unlikely(fp->f_flags&O_APPEND))
		lpos = dev->size;
	if (lpos+count > dev->alloc) {
		size_t alloc = ((lpos+count)/PAGE_SIZE+1)*PAGE_SIZE;
		data = krealloc(dev->data, alloc, GFP_KERNEL);
		if (IS_ERR(data)) {
			len = PTR_ERR(data);
			goto out;
		}
		dev->alloc = alloc;
		dev->data = data;
	}
	data = dev->data+lpos;
	len = count;
	do {
		unsigned long rem = copy_from_user(data, buf, len);
		if (!rem)
			break;
		data += len-rem;
		buf += len-rem;
		len = rem;
	} while (len);
	if (lpos+count > dev->size)
		dev->size = lpos+count;
	*pos = lpos+count;
	len = count;
out:
	mutex_unlock(&dev->lock);
	return len;
}

static int open(struct inode *ip, struct file *fp)
{
	struct append_device *dev = container_of(ip->i_cdev,
						 struct append_device, cdev);

	printk(KERN_DEBUG "[%s:%d] open\n", dev_name(&dev->base),
	       task_pid_nr(current));
	fp->private_data = dev;
	if ((fp->f_flags&O_ACCMODE) == O_RDONLY)
		return 0;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (fp->f_flags&O_TRUNC) {
		if (dev->data)
			kfree(dev->data);
		dev->data = NULL;
		dev->alloc = 0;
		dev->size = 0;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct append_device *dev = container_of(base, struct append_device,
						 base);
	size_t size;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	size = dev->size;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", size);
}
static DEVICE_ATTR_RO(size);

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct append_device *dev = container_of(base, struct append_device,
						 base);
	size_t alloc;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	alloc = dev->alloc;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", alloc);
}
static DEVICE_ATTR_RO(alloc);

struct attribute *append_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_alloc.attr,
	NULL,
};
ATTRIBUTE_GROUPS(append);

static int __init init_driver(struct append_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.open		= open;
	return 0;
}

static int __init init(void)
{
	struct append_driver *drv = &append_driver;
	struct append_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct append_device *dev;
	char name[8]; /* sizeof(drv->base.name)+2 */
	int i, err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct append_device));
		dev->data		= NULL;
		dev->alloc = dev->size	= 0;
		dev->base.init_name	= name;
		dev->base.groups	= append_groups;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		mutex_init(&dev->lock);
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct append_driver *drv = &append_driver;
	struct append_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct append_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("O_APPEND example");
