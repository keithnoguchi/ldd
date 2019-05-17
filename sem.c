/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/semaphore.h>

struct sem_device {
	atomic_t		lockers;
	struct semaphore	lock;
	struct miscdevice	base;
};

static struct sem_driver {
	int			default_sem_count;
	struct file_operations	fops;
	struct device_driver	base;
	struct sem_device	devs[1];
} sem_driver = {
	.default_sem_count	= 1,
	.base.name		= "sem",
	.base.owner		= THIS_MODULE,
};
module_param_named(default_sem_count, sem_driver.default_sem_count, int, S_IRUGO);

static int open(struct inode *ip, struct file *fp)
{
	struct sem_device *dev = container_of(fp->private_data, struct sem_device, base);
	const struct sem_driver *const drv = &sem_driver;
	struct miscdevice *misc = fp->private_data;
	int lockers;

	printk(KERN_DEBUG "[%s:%d]: semaphore aquiring...\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	if (down_interruptible(&dev->lock))
		return -ERESTARTSYS;
	lockers = atomic_inc_return(&dev->lockers);
	if (lockers > drv->default_sem_count) {
		printk(KERN_CRIT "[%s:%d]: lockers=%d > default_sem_count=%d\n",
		       dev_name(misc->this_device), task_pid_nr(current),
		       lockers, drv->default_sem_count);
		atomic_dec(&dev->lockers);
		up(&dev->lock);
		return -EINVAL;
	}
	printk(KERN_DEBUG "[%s:%d]: semaphore aquired\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct sem_device *dev = container_of(fp->private_data, struct sem_device, base);
	struct miscdevice *misc = fp->private_data;
	int lockers;

	printk(KERN_DEBUG "[%s:%d]: semaphre releasing...\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	lockers = atomic_dec_return(&dev->lockers);
	if (lockers < 0) {
		printk(KERN_CRIT "[%s:%d]: lockers(%d) < 0\n",
		       dev_name(misc->this_device), task_pid_nr(current),
		       lockers);
		return -EINVAL;
	}
	up(&dev->lock);
	printk(KERN_DEBUG "[%s:%d]: semaphore released\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	return 0;
}

static ssize_t lockers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct sem_device *dev = container_of(dev_get_drvdata(base), struct sem_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->lockers));
}
static DEVICE_ATTR_RO(lockers);

static struct attribute *sem_attrs[] = {
	&dev_attr_lockers.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sem);

static int __init init_driver(struct sem_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct sem_driver *drv = &sem_driver;
	struct sem_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct sem_device *dev;
	char name[5]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct sem_device));
		atomic_set(&dev->lockers, 0);
		sema_init(&dev->lock, drv->default_sem_count);
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		dev->base.groups	= sem_groups;
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
	struct sem_driver *drv = &sem_driver;
	struct sem_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct sem_device *dev;

	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Semaphore test module");
