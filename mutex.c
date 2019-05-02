/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/mutex.h>

struct mutex_device {
	struct mutex		lock;
	int			lockers;
	struct miscdevice	base;
};

static struct mutex_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct mutex_device	devs[1];
} mutex_driver = {
	.base.name	= "mutex",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct mutex_device *dev = container_of(fp->private_data,
						struct mutex_device, base);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (dev->lockers != 0) {
		struct miscdevice *misc = fp->private_data;
		printk(KERN_CRIT "[%s:%d]: lock is held by other task: %d!=1\n",
		       dev_name(misc->this_device), task_pid_nr(current),
		       dev->lockers);
		mutex_unlock(&dev->lock);
		return -EINVAL;
	}
	dev->lockers++;
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct mutex_device *dev = container_of(fp->private_data,
						struct mutex_device, base);
	int lockers;

	lockers = --dev->lockers;
	mutex_unlock(&dev->lock);
	/* lock counter should be zero */
	if (lockers != 0) {
		struct miscdevice *misc = fp->private_data;
		printk(KERN_CRIT "[%s:%d] lock is held by other tasks: %d!=0\n",
		       dev_name(misc->this_device), task_pid_nr(current),
		       lockers);
		return -EINVAL;
	}
	return 0;
}

static ssize_t lockers_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct mutex_device *dev = container_of(dev_get_drvdata(base),
						struct mutex_device, base);
	int lockers;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	lockers = dev->lockers;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%d\n", lockers);
}
static DEVICE_ATTR_RO(lockers);

static struct attribute *mutex_attrs[] = {
	&dev_attr_lockers.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mutex);

static void __init init_driver(struct mutex_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct mutex_driver *drv = &mutex_driver;
	struct mutex_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct mutex_device *dev;
	char name[7];
	int i, err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct mutex_device));
		mutex_init(&dev->lock);
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		dev->base.groups	= mutex_groups;
		err = misc_register(&dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct mutex_driver *drv = &mutex_driver;
	struct mutex_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct mutex_device *dev;

	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Mutex test module");
