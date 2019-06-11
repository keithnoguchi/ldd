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
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

struct scullpipe_device {
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
	return dev->rpos == dev->wpos;
}

static int is_full(const struct scullpipe_device *const dev)
{
	return (dev->wpos+1)%dev->bufsiz == dev->rpos;
}

static ssize_t data(const struct scullpipe_device *const dev)
{
	if (is_empty(dev))
		return 0;
	else if (dev->rpos < dev->wpos)
		return dev->wpos-dev->rpos;
	else /* wrapped */
		return dev->bufsiz-(dev->rpos-dev->wpos);
}

static ssize_t space(const struct scullpipe_device *const dev)
{
	if (is_full(dev))
		return 0;
	else if (dev->wpos < dev->rpos)
		return dev->rpos-dev->wpos-1;
	else /* wrapped */
		return dev->bufsiz-(dev->wpos-dev->rpos)-1;
}

static void *readp(const struct scullpipe_device *const dev)
{
	return dev->buf+dev->rpos;
}

static void *writep(const struct scullpipe_device *const dev)
{
	return dev->buf+dev->wpos;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct scullpipe_device *dev = fp->private_data;
	unsigned long remain;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	while (is_empty(dev)) {
		DEFINE_WAIT(w);
		mutex_unlock(&dev->lock);
		if (fp->f_flags&O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_DEBUG "[%s:%d] read block\n", dev_name(&dev->base),
		       task_pid_nr(current));
		prepare_to_wait(&dev->inq, &w, TASK_INTERRUPTIBLE);
		if (is_empty(dev))
			schedule();
		finish_wait(&dev->inq, &w);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	ret = data(dev);
	if (ret > count)
		ret = count;
	remain = ret;
	do {
		unsigned int len = copy_to_user(buf, readp(dev), remain);
		dev->rpos += remain-len;
		buf += remain-len;
		if (dev->rpos >= dev->bufsiz)
			dev->rpos -= dev->bufsiz;
		remain = len;
	} while (remain);
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->outq);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct scullpipe_device *dev = fp->private_data;
	unsigned int remain;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	while (is_full(dev)) {
		DEFINE_WAIT(w);
		mutex_unlock(&dev->lock);
		if (fp->f_flags&O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_DEBUG "[%s:%d] write block\n", dev_name(&dev->base),
		       task_pid_nr(current));
		prepare_to_wait(&dev->outq, &w, TASK_INTERRUPTIBLE);
		if (is_full(dev))
			schedule();
		finish_wait(&dev->outq, &w);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	ret = space(dev);
	if (ret > count)
		ret = count;
	remain = ret;
	do {
		unsigned int len = copy_from_user(writep(dev), buf, remain);
		dev->wpos += remain-len;
		buf += remain-len;
		if (dev->wpos >= dev->bufsiz)
			dev->wpos -= dev->bufsiz;
		remain = len;
	} while (remain);
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->inq);
	return ret;
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
	unsigned int val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->readers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}
static DEVICE_ATTR_RO(readers);

static ssize_t writers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	unsigned int val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->writers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}
static DEVICE_ATTR_RO(writers);

static ssize_t bufsiz_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
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
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	ssize_t ret;
	long val;

	ret = kstrtol(page, 10, &val);
	if (ret)
		return ret;
	if (val < 0 || val > SIZE_MAX)
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	ret = -EINVAL;
	if (!is_empty(dev))
		goto out;
	if (dev->alloc < val) {
		/* PAGE_SIZE aligned buffer size */
		size_t alloc = ((val-1)/PAGE_SIZE+1)*PAGE_SIZE;
		void *buf = krealloc(dev->buf, alloc, GFP_KERNEL);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			goto out;
		}
		dev->alloc = alloc;
		dev->buf = buf;
	}
	dev->bufsiz = val;
	dev->rpos = dev->wpos = 0;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}
static DEVICE_ATTR_RW(bufsiz);

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	size_t val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->alloc;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", val);
}
static DEVICE_ATTR_RO(alloc);

static ssize_t is_empty_show(struct device *base, struct device_attribute *attr,
			     char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	int val = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = is_empty(dev);
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", val);
}
static DEVICE_ATTR_RO(is_empty);

static ssize_t is_full_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct scullpipe_device *dev = container_of(base,
						    struct scullpipe_device,
						    base);
	int val = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = is_full(dev);
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", val);
}
static DEVICE_ATTR_RO(is_full);

static struct attribute *scullpipe_attrs[] = {
	&dev_attr_readers.attr,
	&dev_attr_writers.attr,
	&dev_attr_bufsiz.attr,
	&dev_attr_alloc.attr,
	&dev_attr_is_empty.attr,
	&dev_attr_is_full.attr,
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
		init_waitqueue_head(&dev->inq);
		init_waitqueue_head(&dev->outq);
		dev->readers		= 0;
		dev->writers		= 0;
		dev->rpos = dev->wpos	= 0;
		dev->bufsiz		= drv->default_bufsiz;
		dev->alloc		= ((dev->bufsiz-1)/PAGE_SIZE+1)*PAGE_SIZE;
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
