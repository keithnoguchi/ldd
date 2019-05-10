/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct spinlock_context {
	struct spinlock_context	*next;
	struct file		*fp;
	unsigned int		count;
};

struct spinlock_device {
	spinlock_t		lock;
	struct spinlock_context	*head;
	struct spinlock_context	*free;
	struct miscdevice	base;
};

static struct spinlock_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct spinlock_device	devs[2];
} spinlock_driver = {
	.base.name	= "spinlock",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct spinlock_device *dev = container_of(fp->private_data,
						   struct spinlock_device,
						   base);
	struct spinlock_context **ctx, *tmp;

	/* get the context from the free list if there is */
	spin_lock(&dev->lock);
	tmp = dev->free;
	if (tmp)
		dev->free = tmp->next;
	spin_unlock(&dev->lock);
	if (!tmp) {
		tmp = kzalloc(sizeof(struct spinlock_context), GFP_KERNEL);
		if (IS_ERR(tmp))
			return PTR_ERR(tmp);
	}
	tmp->next = NULL;
	tmp->count = 1;
	tmp->fp = fp;
	/* just make counter up if there is a context already */
	spin_lock(&dev->lock);
	for (ctx = &dev->head; *ctx; ctx = &(*ctx)->next)
		if ((*ctx)->fp == fp) {
			++(*ctx)->count;
			goto out;
		}
	/* no entry, let's add to the end */
	*ctx = tmp;
	tmp = NULL;
out:
	if (tmp) {
		tmp->next = dev->free;
		dev->free = tmp;
	}
	spin_unlock(&dev->lock);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct spinlock_device *dev = container_of(fp->private_data,
						   struct spinlock_device,
						   base);
	struct spinlock_context **ctx, *got = NULL;

	spin_lock(&dev->lock);
	for (ctx = &dev->head; *ctx; ctx = &(*ctx)->next)
		if ((*ctx)->fp == fp) {
			got = *ctx;
			if (--(*ctx)->count == 0) {
				*ctx = (*ctx)->next;
				got->next = dev->free;
				dev->free = got;
			}
			goto out;
		}
out:
	spin_unlock(&dev->lock);
	if (!got)
		return -EINVAL;
	return 0;
}

static ssize_t active_show(struct device *base,
			   struct device_attribute *attr,
			   char *page)
{
	struct spinlock_device *dev = container_of(dev_get_drvdata(base),
						   struct spinlock_device,
						   base);
	struct spinlock_context *ctx;
	size_t nr = 0;

	spin_lock(&dev->lock);
	for (ctx = dev->head; ctx; ctx = ctx->next)
		nr++;
	spin_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", nr);
}
static DEVICE_ATTR_RO(active);

static ssize_t free_show(struct device *base,
			 struct device_attribute *attr,
			 char *page)
{
	struct spinlock_device *dev = container_of(dev_get_drvdata(base),
						   struct spinlock_device,
						   base);
	struct spinlock_context *ctx;
	size_t nr = 0;

	spin_lock(&dev->lock);
	for (ctx = dev->free; ctx; ctx = ctx->next)
		nr++;
	spin_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", nr);
}
static DEVICE_ATTR_RO(free);

static struct attribute *spinlock_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_free.attr,
	NULL,
};
ATTRIBUTE_GROUPS(spinlock);

static int __init init_driver(struct spinlock_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct spinlock_driver *drv = &spinlock_driver;
	struct spinlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct spinlock_device *dev;
	char name[10]; /* strlen(drv->base.name)+2 */
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
		memset(dev, 0, sizeof(struct spinlock_device));
		spin_lock_init(&dev->lock);
		dev->head = dev->free	= NULL;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		dev->base.groups	= spinlock_groups;
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
	struct spinlock_driver *drv = &spinlock_driver;
	struct spinlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct spinlock_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		struct spinlock_context *ctx, *next;
		for (ctx = dev->head; ctx; ctx = next) {
			next = ctx->next;
			kfree(ctx);
		}
		for (ctx = dev->free; ctx; ctx = next) {
			next = ctx->next;
			kfree(ctx);
		}
		misc_deregister(&dev->base);
	}
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Spin lock test module");
