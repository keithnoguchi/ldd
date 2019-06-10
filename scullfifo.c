/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>

struct scullfifo_device {
	struct mutex	lock;
	unsigned int	readers;
	unsigned int	writers;
	struct cdev	cdev;
	struct device	base;
};

static struct scullfifo_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_type	type;
	struct device_driver	base;
	struct scullfifo_device	devs[2];
} scullfifo_driver = {
	.base.name	= "scullfifo",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct scullfifo_device *dev = container_of(ip->i_cdev,
						    struct scullfifo_device,
						    cdev);

	fp->private_data = dev;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	switch (fp->f_flags&O_ACCMODE) {
	case O_RDWR:
		dev->readers++;
		/* fall through */
	case O_WRONLY:
		dev->writers++;
		break;
	default:
		dev->readers++;
		break;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct scullfifo_device *dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	switch (fp->f_flags&O_ACCMODE) {
	case O_RDWR:
		dev->readers--;
		/* fall through */
	case O_WRONLY:
		dev->writers--;
		break;
	default:
		dev->readers--;
		break;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t readers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullfifo_device *dev = container_of(base,
						    struct scullfifo_device,
						    base);
	unsigned int val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->readers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", val);
}
static DEVICE_ATTR_RO(readers);

static ssize_t writers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullfifo_device *dev = container_of(base,
						    struct scullfifo_device,
						    base);
	unsigned int val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->writers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", val);
}
static DEVICE_ATTR_RO(writers);

static struct attribute *scullfifo_attrs[] = {
	&dev_attr_readers.attr,
	&dev_attr_writers.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scullfifo);

static int __init init_driver(struct scullfifo_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->type, 0, sizeof(struct device_type));
	drv->type.groups	= scullfifo_groups;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct scullfifo_driver *drv = &scullfifo_driver;
	struct scullfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullfifo_device *dev;
	char name[11]; /* strlen(drv->base.name)+2 */
	int err, i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct scullfifo_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		dev->readers		= 0;
		dev->writers		= 0;
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
	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scullfifo_driver *drv = &scullfifo_driver;
	struct scullfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullfifo_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguhi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull fifo device driver");

