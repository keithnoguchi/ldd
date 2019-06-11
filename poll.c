/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/poll.h>

struct poll_device {
	wait_queue_head_t	inq;
	wait_queue_head_t	outq;
	struct mutex		lock;
	void			*buf;
	size_t			rpos;
	size_t			wpos;
	size_t			bufsiz;
	size_t			alloc;
	struct cdev		cdev;
	struct device		base;
};

static struct poll_driver {
	size_t			default_bufsiz;
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct poll_device	devs[3];
} poll_driver = {
	.default_bufsiz	= PAGE_SIZE,
	.base.name	= "poll",
	.base.owner	= THIS_MODULE,
};

static int is_empty(const struct poll_device *const dev)
{
	return dev->wpos == dev->rpos;
}

static int is_full(const struct poll_device *const dev)
{
	return (dev->wpos+1)%dev->bufsiz == dev->rpos;
}

static size_t datalen(const struct poll_device *const dev)
{
	if (is_full(dev))
		return dev->bufsiz-1;
	else if (is_empty(dev))
		return 0;
	else if (dev->wpos > dev->rpos)
		return dev->wpos-dev->rpos;
	else
		return dev->wpos+dev->bufsiz-dev->rpos;
}

static size_t buflen(const struct poll_device *const dev)
{
	if (is_empty(dev))
		return dev->bufsiz-1;
	else if (is_full(dev))
		return 0;
	else if (dev->rpos > dev->wpos)
		return dev->rpos-dev->wpos-1;
	else
		return dev->rpos-1+dev->bufsiz-dev->wpos;
}

static size_t allocsiz(const struct poll_device *const dev)
{
	BUG_ON(dev->bufsiz < 1);
	return ((dev->bufsiz-1)/PAGE_SIZE+1)*PAGE_SIZE;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct poll_device *dev = fp->private_data;
	ssize_t ret = -EAGAIN;

	if (!(fp->f_flags&O_NONBLOCK))
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (is_empty(dev))
		goto out;
	ret = datalen(dev);
	if (ret > count)
		ret = count;
	if (ret >= dev->bufsiz-dev->rpos) {
		/* no wrap around read support */
		ret = dev->bufsiz-dev->rpos;
		dev->rpos = 0;
	} else
		dev->rpos += ret;
	*pos += ret;
	wake_up_interruptible(&dev->outq);
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct poll_device *dev = fp->private_data;
	ssize_t ret = -EAGAIN;

	if (!(fp->f_flags&O_NONBLOCK))
		return -EINVAL;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (is_full(dev))
		goto out;
	ret = buflen(dev);
	if (ret > count)
		ret = count;
	if (ret >= dev->bufsiz-dev->wpos) {
		/* no wrap around write support */
		ret = dev->bufsiz-dev->wpos;
		dev->wpos = 0;
	} else
		dev->wpos += ret;
	*pos += ret;
	wake_up_interruptible(&dev->inq);
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static __poll_t poll(struct file *fp, poll_table *p)
{
	struct poll_device *dev = fp->private_data;
	__poll_t mask = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	poll_wait(fp, &dev->inq, p);
	poll_wait(fp, &dev->outq, p);
	if (!is_empty(dev))
		mask |= POLLIN|POLLRDNORM;
	if (!is_full(dev))
		mask |= POLLOUT|POLLWRNORM;
	mutex_unlock(&dev->lock);
	return mask;
}

static int open(struct inode *ip, struct file *fp)
{
	struct poll_device *dev = container_of(ip->i_cdev, struct poll_device,
					       cdev);
	fp->private_data = dev;
	return 0;
}

static int __init init_driver(struct poll_driver *drv)
{
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.read	= read;
	drv->fops.write	= write;
	drv->fops.poll	= poll;
	drv->fops.open	= open;
	return 0;
}

static int __init init(void)
{
	struct poll_driver *drv = &poll_driver;
	struct poll_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct poll_device *dev;
	char name[6]; /* strlen(drv->base.name)+2 */
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
		memset(dev, 0, sizeof(struct poll_device));
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		init_waitqueue_head(&dev->inq);
		init_waitqueue_head(&dev->outq);
		dev->rpos = dev->wpos	= 0;
		dev->bufsiz		= drv->default_bufsiz;
		dev->alloc		= allocsiz(dev);
		dev->buf		= kmalloc(dev->alloc, GFP_KERNEL);
		if (IS_ERR(dev->buf)) {
			err = PTR_ERR(dev->buf);
			end = dev;
			goto err;
		}
		dev->base.init_name	= name;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
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
	struct poll_driver *drv = &poll_driver;
	struct poll_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct poll_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("select(2), poll(2), epoll(7) test driver");
