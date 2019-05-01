/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/rwsem.h>

struct rwsem_device {
	atomic_t		readers;
	atomic_t		writers;
	struct rw_semaphore	lock;
	struct miscdevice	base;
};

static struct rwsem_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct rwsem_device	devs[1];
} rwsem_driver = {
	.base.name	= "rwsem",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct rwsem_device *dev = container_of(fp->private_data,
						struct rwsem_device, base);
	struct miscdevice *misc = fp->private_data;
	int (*down)(struct rw_semaphore *);
	atomic_t *counter;
	int err;

	printk(KERN_DEBUG "[%s:%d]: semaphore aquiring...",
	       dev_name(misc->this_device), task_pid_nr(current));
	if ((fp->f_flags&O_ACCMODE)&O_WRONLY) {
		down = down_write_killable;
		counter = &dev->writers;
	} else {
		down = down_read_killable;
		counter = &dev->readers;
	}
	err = (*down)(&dev->lock);
	if (err)
		return -ERESTARTSYS;
	atomic_inc(counter);
	printk(KERN_DEBUG "[%s:%d]: semaphore aquired",
	       dev_name(misc->this_device), task_pid_nr(current));
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct rwsem_device *dev = container_of(fp->private_data,
						struct rwsem_device, base);
	struct miscdevice *misc = fp->private_data;
	void (*up)(struct rw_semaphore *);
	atomic_t *counter;

	printk(KERN_DEBUG "[%s:%d] semaphore releasing...\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	if ((fp->f_flags&O_ACCMODE)&O_WRONLY) {
		up = up_write;
		counter = &dev->writers;
	} else {
		up = up_read;
		counter = &dev->readers;
	}
	(*up)(&dev->lock);
	atomic_dec(counter);
	printk(KERN_DEBUG "[%s:%d] semaphore released\n",
	       dev_name(misc->this_device), task_pid_nr(current));
	return 0;
}

static ssize_t readers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct rwsem_device *dev = container_of(dev_get_drvdata(base),
						struct rwsem_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->readers));
}
static DEVICE_ATTR_RO(readers);

static ssize_t writers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct rwsem_device *dev = container_of(dev_get_drvdata(base),
						struct rwsem_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->writers));
}
static DEVICE_ATTR_RO(writers);

static ssize_t lockers_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct rwsem_device *dev = container_of(dev_get_drvdata(base),
						struct rwsem_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n",
			atomic_read(&dev->readers)+atomic_read(&dev->writers));
}
static DEVICE_ATTR_RO(lockers);

static struct attribute *rwsem_attrs[] = {
	&dev_attr_readers.attr,
	&dev_attr_writers.attr,
	&dev_attr_lockers.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rwsem);

static void __init init_driver(struct rwsem_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
}

static int __init init(void)
{
	struct rwsem_driver *drv = &rwsem_driver;
	struct rwsem_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rwsem_device *dev;
	char name[7]; /* sizeof(drv->base.name)+2 */
	int i, err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%i", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct rwsem_device));
		atomic_set(&dev->readers, 0);
		atomic_set(&dev->writers, 0);
		init_rwsem(&dev->lock);
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		dev->base.groups	= rwsem_groups;
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
	struct rwsem_driver *drv = &rwsem_driver;
	struct rwsem_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rwsem_device *dev;

	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("R/W semaphores test module");
