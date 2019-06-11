/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

struct scullfifo_device {
	wait_queue_head_t	inq;
	wait_queue_head_t	outq;
	struct mutex		lock;
	void			*buf;
	size_t			rpos;
	size_t			wpos;
	size_t			bufsiz;
	size_t			alloc;
	unsigned int		readers;
	unsigned int		writers;
	struct cdev		cdev;
	struct device		base;
};

static struct scullfifo_driver {
	size_t			minimum_bufsiz;
	size_t			default_bufsiz;
	size_t			maximum_bufsiz;
	dev_t			devt;
	struct file_operations	fops;
	struct device_type	type;
	struct device_driver	base;
	struct scullfifo_device	devs[2];
} scullfifo_driver = {
	.minimum_bufsiz	= 1,
	.default_bufsiz	= PAGE_SIZE,
	.maximum_bufsiz	= PAGE_SIZE*2,
	.base.name	= "scullfifo",
	.base.owner	= THIS_MODULE,
};

static int is_empty(const struct scullfifo_device *const dev)
{
	return dev->rpos == dev->wpos;
}

static int is_full(const struct scullfifo_device *const dev)
{
	return (dev->rpos+dev->wpos+1)%dev->bufsiz == 0;
}

static size_t datalen(const struct scullfifo_device *const dev)
{
	if (is_full(dev))
		return dev->bufsiz-1;
	else if (is_empty(dev))
		return 0;
	else if (dev->wpos > dev->rpos)
		return dev->wpos-dev->rpos;
	else
		return dev->bufsiz-dev->rpos+dev->wpos;
}

static size_t space(const struct scullfifo_device *const dev)
{
	if (is_empty(dev))
		return dev->bufsiz-1;
	else if (is_full(dev))
		return 0;
	else if (dev->rpos > dev->wpos)
		return dev->rpos-dev->wpos-1;
	else
		return dev->bufsiz-dev->wpos+dev->rpos-1;
}

static size_t allocsiz(const struct scullfifo_device *const dev)
{
	/* dev->bufsiz should be greater than 0 */
	return ((dev->bufsiz-1)/PAGE_SIZE+1)*PAGE_SIZE;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct scullfifo_device *dev = fp->private_data;
	size_t ret, rem;
	void *ptr;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	while (is_empty(dev)) {
		if (dev->writers == 0) {
			ret = 0;
			goto out;
		}
		mutex_unlock(&dev->lock);
		if (fp->f_flags&O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(dev->inq, !is_empty(dev));
		if (ret)
			return ret;
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	ret = datalen(dev);
	if (ret > count)
		ret = count;
	/* no wrapped read */
	if (dev->rpos+ret > dev->bufsiz)
		ret = dev->bufsiz-dev->rpos;
	ptr = dev->buf+dev->rpos;
	rem = ret;
	while (rem) {
		rem = copy_to_user(buf, ptr, rem);
		buf += ret-rem;
		ptr += ret-rem;
	}
	dev->rpos = (dev->rpos+ret)%dev->bufsiz;
	*pos += ret;
	wake_up_interruptible(&dev->outq);
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct scullfifo_device *dev = fp->private_data;
	size_t ret, rem;
	void *ptr;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	while (is_full(dev)) {
		mutex_unlock(&dev->lock);
		if (fp->f_flags&O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(dev->outq, !is_full(dev));
		if (ret)
			return ret;
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	ret = space(dev);
	if (ret > count)
		ret = count;
	/* no wrapped write */
	if (dev->wpos+ret > dev->bufsiz)
		ret = dev->bufsiz-dev->wpos;
	ptr = dev->buf+dev->wpos;
	rem = ret;
	while (rem) {
		rem = copy_from_user(ptr, buf, rem);
		ptr += ret-rem;
		buf += ret-rem;
	}
	dev->wpos = (dev->wpos+ret)%dev->bufsiz;
	*pos += ret;
	wake_up_interruptible(&dev->inq);
	mutex_unlock(&dev->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scullfifo_device *dev = container_of(ip->i_cdev,
						    struct scullfifo_device,
						    cdev);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	fp->private_data = dev;
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

static ssize_t bufsiz_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct scullfifo_device *dev = container_of(base,
						    struct scullfifo_device,
						    base);
	size_t val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->bufsiz;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", val);
}

static ssize_t bufsiz_store(struct device *base, struct device_attribute *attr,
			    const char *page, size_t count)
{
	struct scullfifo_device *dev = container_of(base,
						    struct scullfifo_device,
						    base);
	struct scullfifo_driver *drv = dev_get_drvdata(base);
	size_t alloc;
	void *buf;
	long val;
	int err;

	err = kstrtol(page, 10, &val);
	if (err)
		return err;
	if (val < drv->minimum_bufsiz || val > drv->maximum_bufsiz)
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (dev->readers || dev->writers) {
		err = -EPERM;
		goto out;
	}
	dev->bufsiz = val;
	dev->rpos = 0;
	dev->wpos = 0;
	err = count;
	alloc = allocsiz(dev);
	if (alloc <= dev->alloc)
		goto out;
	buf = krealloc(dev->buf, alloc, GFP_KERNEL);
	if (IS_ERR(buf)) {
		err = PTR_ERR(buf);
		goto out;
	}
	dev->alloc = alloc;
	dev->buf = buf;
out:
	mutex_unlock(&dev->lock);
	return err;
}
static DEVICE_ATTR_RW(bufsiz);

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct scullfifo_device *dev = container_of(base,
						    struct scullfifo_device,
						    base);
	size_t val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->alloc;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", val);
}
static DEVICE_ATTR_RO(alloc);

static struct attribute *scullfifo_attrs[] = {
	&dev_attr_readers.attr,
	&dev_attr_writers.attr,
	&dev_attr_bufsiz.attr,
	&dev_attr_alloc.attr,
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
	drv->fops.read		= read;
	drv->fops.write		= write;
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
		memset(dev, 0, sizeof(struct scullfifo_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		init_waitqueue_head(&dev->inq);
		init_waitqueue_head(&dev->outq);
		dev->bufsiz		= drv->default_bufsiz;
		dev->rpos		= 0;
		dev->wpos		= 0;
		dev->readers		= 0;
		dev->writers		= 0;
		dev->base.init_name	= name;
		dev->base.driver_data	= drv;
		dev->base.type		= &drv->type;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->alloc		= allocsiz(dev);
		dev->buf = kmalloc(dev->alloc, GFP_KERNEL);
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
	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scullfifo_driver *drv = &scullfifo_driver;
	struct scullfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullfifo_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull fifo device driver");
