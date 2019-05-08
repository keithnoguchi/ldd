/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/completion.h>

struct comp_device {
	atomic_t		waiters;
	struct completion	done;
	struct miscdevice	base;
};

static struct comp_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct comp_device	devs[4];
} comp_driver = {
	.base.name	= "comp",
	.base.owner	= THIS_MODULE,
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct comp_device *dev = container_of(fp->private_data,
					       struct comp_device, base);
	int err = 0;

	atomic_inc(&dev->waiters);
	printk(KERN_DEBUG "[%s:%d] wait(waiters=%d)\n",
	       dev_name(dev->base.this_device), task_pid_nr(current),
	       atomic_read(&dev->waiters));
	if (wait_for_completion_interruptible(&dev->done))
		err = -ERESTARTSYS;
	atomic_dec(&dev->waiters);
	return err;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct comp_device *dev = container_of(fp->private_data,
					       struct comp_device, base);
	printk(KERN_DEBUG "[%s:%d] complete(waiters=%d)\n",
	       dev_name(dev->base.this_device), task_pid_nr(current),
	       atomic_read(&dev->waiters));
	/* notify the single waiter */
	complete(&dev->done);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct comp_device *dev = container_of(fp->private_data,
					       struct comp_device, base);
	printk(KERN_DEBUG "[%s:%d] release(waiters=%d)\n", dev_name(dev->base.this_device),
	       task_pid_nr(current), atomic_read(&dev->waiters));
	/* notify all the waiters */
	complete_all(&dev->done);
	reinit_completion(&dev->done);
	return 0;
}

static ssize_t waiters_show(struct device *base, struct device_attribute *attr,
			    char *page)
{
	struct comp_device *dev = container_of(dev_get_drvdata(base),
					       struct comp_device, base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->waiters));
}
static DEVICE_ATTR_RO(waiters);

static struct attribute *comp_attrs[] = {
	&dev_attr_waiters.attr,
	NULL,
};
ATTRIBUTE_GROUPS(comp);

static int __init init_driver(struct comp_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct comp_driver *drv = &comp_driver;
	struct comp_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct comp_device *dev;
	char name[6]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%i", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct comp_device));
		init_completion(&dev->done);
		atomic_set(&dev->waiters, 0);
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		dev->base.groups	= comp_groups;
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
	struct comp_driver *drv = &comp_driver;
	struct comp_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct comp_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Completion test module");
