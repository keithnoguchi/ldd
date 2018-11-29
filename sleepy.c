/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/semaphore.h>

struct sleepy_device {
	struct semaphore	lock;
	int			condition;
	wait_queue_head_t	wq;
	struct miscdevice	dev;
};

/* sleep device file operations */
static ssize_t sleepy_read(struct file *f, char __user *buf, size_t nr, loff_t *pos)
{
	struct sleepy_device *d = container_of(f->private_data, struct sleepy_device, dev);
	struct device *dev = d->dev.this_device;

	if (down_interruptible(&d->lock))
		return -ERESTARTSYS;
	while (!d->condition) {
		up(&d->lock);
		if (wait_event_interruptible(d->wq, d->condition))
			return -ERESTARTSYS;
		if (down_interruptible(&d->lock))
			return -ERESTARTSYS;
	}
	printk(KERN_INFO "read(%s)\n", dev_name(dev));
	d->condition = 0;
	up(&d->lock);

	return 0;
}

static ssize_t sleepy_write(struct file *f, const char __user *buf, size_t nr, loff_t *pos)
{
	struct sleepy_device *d = container_of(f->private_data, struct sleepy_device, dev);
	struct device *dev = d->dev.this_device;
	printk(KERN_INFO "write(%s)\n", dev_name(dev));
	return 0;
}

static int sleepy_release(struct inode *i, struct file *f)
{
	struct sleepy_device *d = container_of(f->private_data, struct sleepy_device, dev);
	struct device *dev = d->dev.this_device;
	printk(KERN_INFO "release(%s)\n", dev_name(dev));
	return 0;
}

static const struct file_operations sleepy_fops = {
	.read		= sleepy_read,
	.write		= sleepy_write,
	.release	= sleepy_release,
};

/* miscdevice based sleepy device */
static struct sleepy_device devices[] = {
	{
		.dev.minor	= MISC_DYNAMIC_MINOR,
		.dev.name	= "sleepy0",
		.dev.fops	= &sleepy_fops,
	},
	{
		.dev.minor	= MISC_DYNAMIC_MINOR,
		.dev.name	= "sleepy1",
		.dev.fops	= &sleepy_fops,
	},
	{},	/* sentry */
};

int sleepy_register(void)
{
	struct sleepy_device *d, *d_err;
	int err;

	for (d = devices; d->dev.name; d++) {
		sema_init(&d->lock, 1);
		d->condition = 0;
		init_waitqueue_head(&d->wq);
		if ((err = misc_register(&d->dev))) {
			d_err = d;
			goto out;
		}
	}
	return 0;
out:
	for (d = devices; d != d_err; d++)
		misc_deregister(&d->dev);
	return err;
}

void sleepy_unregister(void)
{
	struct sleepy_device *d;

	for (d = devices; d->dev.name; d++) {
		down(&d->lock);
		wake_up_all(&d->wq);
		up(&d->lock);
		misc_deregister(&d->dev);
	}
}
