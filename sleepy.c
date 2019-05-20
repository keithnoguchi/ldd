/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

struct sleepy_device {
	spinlock_t		lock;
	int			ready;
	wait_queue_head_t	waitq;
	struct miscdevice	base;
};

static struct sleepy_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct sleepy_device	devs[2];
} sleepy_driver = {
	.base.name	= "sleepy",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct sleepy_device *dev = container_of(fp->private_data,
						 struct sleepy_device,
						 base);

	spin_lock(&dev->lock);
	while (dev->ready <= 0) {
		spin_unlock(&dev->lock);
		if (wait_event_interruptible(dev->waitq, dev->ready > 0))
			return -ERESTARTSYS;
		spin_lock(&dev->lock);
	}
	dev->ready--;
	spin_unlock(&dev->lock);
	return 0; /* EOF */
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct sleepy_device *dev = container_of(fp->private_data,
						 struct sleepy_device,
						 base);

	spin_lock(&dev->lock);
	dev->ready++;
	spin_unlock(&dev->lock);
	wake_up_interruptible(&dev->waitq);
	return count; /* avoid retry */
}

static ssize_t ready_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct sleepy_device *dev = container_of(dev_get_drvdata(base),
						 struct sleepy_device,
						 base);
	int ready;

	spin_lock(&dev->lock);
	ready = dev->ready;
	spin_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", ready);
}

static ssize_t ready_store(struct device *base, struct device_attribute *attr,
			   const char *page, size_t size)
{
	struct sleepy_device *dev = container_of(dev_get_drvdata(base),
						 struct sleepy_device,
						 base);
	long ready;
	int err;

	err = kstrtol(page, 10, &ready);
	if (err)
		return err;
	if (ready < 0 || ready > INT_MAX)
		return -EINVAL;
	spin_lock(&dev->lock);
	dev->ready = ready;
	spin_unlock(&dev->lock);
	printk(KERN_DEBUG "[%s]: ready=%ld, size=%ld\n", dev_name(base), ready, size);
	return size;
}
static DEVICE_ATTR_RW(ready);

static struct attribute *sleepy_attrs[] = {
	&dev_attr_ready.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sleepy);

static int __init init_driver(struct sleepy_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.read	= read;
	drv->fops.write	= write;
	return 0;
}

static int __init init(void)
{
	struct sleepy_driver *drv = &sleepy_driver;
	struct sleepy_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct sleepy_device *dev;
	char name[8]; /* strlen(drv->base.name)+2 */
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
		memset(dev, 0, sizeof(struct sleepy_device));
		spin_lock_init(&dev->lock);
		init_waitqueue_head(&dev->waitq);
		dev->ready		= 0;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.groups	= sleepy_groups;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		err = misc_register(&dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct sleepy_driver *drv = &sleepy_driver;
	struct sleepy_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct sleepy_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/wait.h> test module");
