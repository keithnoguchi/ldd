/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

/* Scull driver */
static struct device_driver driver = {
	.name	= "scull",
};

/* Scull devices */
static struct scull_device {
	struct semaphore	sem;
	size_t			size;
	size_t			bufsiz;
	void			*data;
	struct device		dev;
	struct cdev		cdev;
} devices[] = {
	{
		.dev.init_name	= "scull0",
	},
	{},	/* sentry */
};

/* Scull device attributes */
static ssize_t pagesize_show(struct device *dev, struct device_attribute *attr,
			     char *page)
{
	return snprintf(page, PAGE_SIZE, "%ld\n", PAGE_SIZE);
}
DEVICE_ATTR_RO(pagesize);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->size);
}
DEVICE_ATTR_RO(size);

static ssize_t bufsize_show(struct device *dev, struct device_attribute *attr,
			    char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->bufsiz);
}
DEVICE_ATTR_RO(bufsize);

static struct attribute *scull_attrs[] = {
	&dev_attr_pagesize.attr,
	&dev_attr_size.attr,
	&dev_attr_bufsize.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scull);

/* Scull device type to carry the device attributes. */
static struct device_type device_type = {
	.name	= "scull",
	.groups	= scull_groups,
};

/* Scull device file operations */
static int scull_open(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);

	printk(KERN_INFO "open(%s)\n", dev_name(&d->dev));
	f->private_data = d;
	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	/* reset the file size when it's opened for write only */
	if ((f->f_flags&O_ACCMODE) & O_WRONLY) {
		kfree(d->data);
		d->data = NULL;
		d->bufsiz = 0;
		d->size = 0;
	}
	up(&d->sem);
	return 0;
}

static ssize_t scull_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct scull_device *d = f->private_data;

	printk(KERN_INFO "read(%s:%ld)\n", dev_name(&d->dev), len);
	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	up(&d->sem);
	return len;
}

static ssize_t scull_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	struct scull_device *d = f->private_data;
	ssize_t ret = 0;

	printk(KERN_INFO "write(%s:%ld)\n", dev_name(&d->dev), len);
	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	if (*pos+len > d->size) {
		/* as simple as this for now */
		if (d->data)
			kfree(d->data);
		d->data = kmalloc(*pos+len, GFP_KERNEL);
		if (d->data == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		d->bufsiz = *pos+len;
		d->size = d->bufsiz;
	}
	ret = len;
	*pos += len;
out:
	up(&d->sem);
	return ret;
}

static int scull_release(struct inode *i, struct file *f)
{
	struct scull_device *d = f->private_data;
	printk(KERN_INFO "release(%s)\n", dev_name(&d->dev));
	return 0;
}

static const struct file_operations scull_fops = {
	.open		= scull_open,
	.read		= scull_read,
	.write		= scull_write,
	.release	= scull_release,
};

/* Scull registration */
int __init scull_register(void)
{
	struct scull_device *d, *d_err = NULL;
	dev_t devt;
	int err;
	int i;

	/* allocate scull device region */
	err = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(devices), driver.name);
	if (err)
		return err;

	/* create devices */
	for (d = devices, i = 0; d->dev.init_name; d++, i++) {
		sema_init(&d->sem, 1);
		d->dev.driver = &driver;
		d->dev.type = &device_type;
		d->dev.devt = MKDEV(MAJOR(devt), MINOR(devt)+i);
		device_initialize(&d->dev);
		cdev_init(&d->cdev, &scull_fops);
		err = cdev_device_add(&d->cdev, &d->dev);
		if (err) {
			d_err = d;
			goto out;
		}
	}
	return 0;
out:
	for (d = devices; d != d_err; d++)
		cdev_device_del(&d->cdev, &d->dev);
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	return err;
}

void scull_unregister(void)
{
	struct scull_device *d;
	for (d = devices; dev_name(&d->dev); d++) {
		cdev_device_del(&d->cdev, &d->dev);
		kfree(d->data);
	}
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
}
