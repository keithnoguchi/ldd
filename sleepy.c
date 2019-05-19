/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/wait.h>

struct sleepy_device {
	wait_queue_head_t	wq;
	struct mutex		lock;
	int			ready;
	struct miscdevice	base;
};

static struct sleepy_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct sleepy_device	devs[1];
} sleepy_driver = {
	.base.name	= "sleepy",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct sleepy_device *dev = container_of(fp->private_data,
						 struct sleepy_device,
						 base);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	while (!dev->ready) {
		mutex_unlock(&dev->lock);
		if (wait_event_interruptible(dev->wq, dev->ready))
			return -ERESTARTSYS;
		mutex_lock(&dev->lock);
	}
	dev->ready = 0;
	mutex_unlock(&dev->lock);
	return 0; /* EOF */
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct sleepy_device *dev = container_of(fp->private_data,
						 struct sleepy_device,
						 base);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	dev->ready = 1;
	mutex_unlock(&dev->lock);
	wake_up_interruptible(&dev->wq);
	return count; /* avoid retry */
}

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
		init_waitqueue_head(&dev->wq);
		mutex_init(&dev->lock);
		dev->ready	= 0;
		dev->base.name	= name;
		dev->base.fops	= &drv->fops;
		dev->base.minor	= MISC_DYNAMIC_MINOR;
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
