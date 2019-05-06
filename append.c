/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/sched.h>

struct append_device {
	struct mutex	lock;
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

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	printk(KERN_DEBUG "[%s:%d] read(buf=%p,count=%ld)\n",
	       dev_name(&dev->base), task_pid_nr(current), buf, count);
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct append_device *dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	printk(KERN_DEBUG "[%s:%d] write(buf=%p,count=%ld)\n",
	       dev_name(&dev->base), task_pid_nr(current), buf, count);
	mutex_unlock(&dev->lock);
	return 0;
}

static int open(struct inode *ip, struct file *fp)
{
	struct append_device *dev = container_of(ip->i_cdev,
						 struct append_device, cdev);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	fp->private_data = dev;
	printk(KERN_DEBUG "[%s:%d] open\n", dev_name(&dev->base),
	       task_pid_nr(current));
	mutex_unlock(&dev->lock);
	return 0;
}

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
		mutex_init(&dev->lock);
		cdev_init(&dev->cdev, &drv->fops);
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->base.init_name	= name;
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
