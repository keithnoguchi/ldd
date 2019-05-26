/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/wait.h>

struct scullpipe_device {
	wait_queue_head_t	waitq;
	struct mutex		lock;
	char			*buf;
	char			*end;
	unsigned int		rp;
	unsigned int		wp;
	size_t			bufsiz;
	size_t			alloc;
	unsigned int		readers;
	unsigned int		writers;
	struct cdev		cdev;
	struct device		base;
};

static struct scullpipe_driver {
	size_t			default_bufsiz;
	dev_t			devt;
	struct file_operations	fops;
	struct device_type	type;
	struct device_driver	base;
	struct scullpipe_device	devs[2];
} scullpipe_driver = {
	.default_bufsiz	= PAGE_SIZE,
	.base.name	= "scullpipe",
	.base.owner	= THIS_MODULE,
};

static int is_empty(const struct scullpipe_device *const dev)
{
	return dev->rp == dev->wp;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scullpipe_device *dev = container_of(ip->i_cdev,
						    struct scullpipe_device,
						    cdev);

	fp->private_data = dev;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	switch (fp->f_flags&O_ACCMODE) {
	case O_RDONLY:
		dev->readers++;
		break;
	case O_RDWR:
		dev->readers++;
		/* fallthrough */
	default:
		dev->writers++;
		break;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct scullpipe_device *dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	switch (fp->f_flags&O_ACCMODE) {
	case O_RDONLY:
		dev->readers--;
		break;
	case O_RDWR:
		dev->readers--;
		/* fallthrough */
	default:
		dev->writers--;
		break;
	}
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t readers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	unsigned int readers;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	readers = dev->readers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%u\n", readers);
}
static DEVICE_ATTR_RO(readers);

static ssize_t writers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	unsigned int writers;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	writers = dev->writers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%u\n", writers);
}
static DEVICE_ATTR_RO(writers);

static ssize_t bufsiz_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	size_t bufsiz;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	bufsiz = dev->bufsiz;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", bufsiz);
}

static ssize_t bufsiz_store(struct device *base, struct device_attribute *attr,
			    const char *page, size_t count)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	long bufsiz;
	ssize_t err;

	err = kstrtol(page, 10, &bufsiz);
	if (err)
		return err;
	if (bufsiz < 0 || bufsiz > SIZE_MAX)
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	err = -EINVAL;
	if (!is_empty(dev))
		goto out;
	if (dev->alloc < bufsiz) {
		/* PAGE_SIZE aligned buffer size */
		size_t alloc = ((bufsiz-1)/PAGE_SIZE+1)*PAGE_SIZE;
		void *buf = krealloc(dev->buf, alloc, GFP_KERNEL);
		if (IS_ERR(buf)) {
			err = PTR_ERR(buf);
			goto out;
		}
		dev->alloc = alloc;
		dev->buf = buf;
	}
	dev->bufsiz = bufsiz;
	err = count;
out:
	mutex_unlock(&dev->lock);
	return count;
}
static DEVICE_ATTR_RW(bufsiz);

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	size_t alloc;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	alloc = dev->alloc;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", alloc);
}
static DEVICE_ATTR_RO(alloc);

static struct attribute *scullpipe_attrs[] = {
	&dev_attr_readers.attr,
	&dev_attr_writers.attr,
	&dev_attr_bufsiz.attr,
	&dev_attr_alloc.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scullpipe);

static int __init init_driver(struct scullpipe_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0,
				  ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->type, 0, sizeof(struct device_type));
	drv->type.groups	= scullpipe_groups;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct scullpipe_driver *drv = &scullpipe_driver;
	struct scullpipe_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullpipe_device *dev;
	char name[11]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct scullpipe_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		init_waitqueue_head(&dev->waitq);
		dev->readers		= 0;
		dev->writers		= 0;
		dev->bufsiz		= drv->default_bufsiz;
		dev->alloc		= ((dev->bufsiz-1)/PAGE_SIZE+1)*PAGE_SIZE;
		dev->rp = dev->wp	= 0;
		dev->cdev.owner		= drv->base.owner;
		dev->base.type		= &drv->type;
		dev->base.init_name	= name;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->buf = kzalloc(dev->alloc, GFP_KERNEL);
		if (IS_ERR(dev->buf)) {
			err = PTR_ERR(dev->buf);
			end = dev;
			goto err;
		}
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			kfree(dev->buf);
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
	struct scullpipe_driver *drv = &scullpipe_driver;
	struct scullpipe_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullpipe_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull pipe device driver");
