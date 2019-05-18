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
#include <linux/rcupdate.h>

struct rculock_context {
	struct rculock_context	*next;
	void			*data;
	unsigned int		count;
	struct rculock_device	*dev;
	struct rcu_head		rcu;
};

struct rculock_device {
	struct mutex		lock;
	struct rculock_context	*head;
	struct rculock_context	*free;
	struct miscdevice	base;
};

static struct rculock_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct rculock_device	devs[2];
} rculock_driver = {
	.base.name	= "rculock",
	.base.owner	= THIS_MODULE,
};

static void rculock_context_free(struct rcu_head *head)
{
	struct rculock_context *ctx = container_of(head, struct rculock_context, rcu);
	struct rculock_device *dev = ctx->dev;
	mutex_lock(&dev->lock);
	ctx->next = dev->free;
	dev->free = ctx;
	mutex_unlock(&dev->lock);
}

static int open(struct inode *ip, struct file *fp)
{
	struct rculock_device *dev = container_of(fp->private_data,
						  struct rculock_device,
						  base);
	struct rculock_context **ctxx, *ctx, *old = NULL;
	int err = 0;

	if ((fp->f_flags&O_ACCMODE) == O_RDONLY) {
		unsigned int writers = 0;
		rcu_read_lock();
		ctx = rcu_dereference(dev->head);
		rcu_read_unlock();
		while (ctx) {
			rcu_read_lock();
			ctx = rcu_dereference(ctx->next);
			rcu_read_unlock();
			writers++;
		}
		return 0;
	}
	/* protects from the other writers */
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	for (ctxx = &dev->head; *ctxx; ctxx = &(*ctxx)->next)
		if ((*ctxx)->data == fp)
			break;
	ctx = dev->free;
	if (ctx)
		dev->free = ctx->next;
	else {
		ctx = kzalloc(sizeof(struct rculock_device), GFP_KERNEL);
		if (IS_ERR(ctx)) {
			err = PTR_ERR(ctx);
			goto out;
		}
		ctx->dev = dev;
	}
	old = rcu_dereference(*ctxx);
	if (old) {
		ctx->count = old->count+1;
		ctx->next = old->next;
	} else {
		ctx->count = 1;
		ctx->next = NULL;
	}
	ctx->data = fp;
	rcu_assign_pointer(*ctxx, ctx);
out:
	mutex_unlock(&dev->lock);
	if (old)
		call_rcu(&old->rcu, rculock_context_free);
	return err;
}

static int release(struct inode *ip, struct file *fp)
{
	struct rculock_device *dev = container_of(fp->private_data,
						  struct rculock_device,
						  base);
	struct rculock_context **ctxx, *ctx, *old = NULL;
	int err = 0;

	if ((fp->f_flags&O_ACCMODE) == O_RDONLY) {
		unsigned int writers = 0;
		rcu_read_lock();
		ctx = rcu_dereference(dev->head);
		rcu_read_unlock();
		while (ctx) {
			rcu_read_lock();
			ctx = rcu_dereference(ctx->next);
			rcu_read_unlock();
			writers++;
		};
		return 0;
	}
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	for (ctxx = &dev->head; *ctxx; ctxx = &(*ctxx)->next)
		if ((*ctxx)->data == fp)
			break;
	if (!*ctxx) {
		err = -EINVAL;
		goto out;
	}
	old = rcu_dereference(*ctxx);
	rcu_assign_pointer(*ctxx, NULL);
out:
	mutex_unlock(&dev->lock);
	if (old)
		call_rcu(&old->rcu, rculock_context_free);
	return err;
}

static ssize_t active_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct rculock_device *dev = container_of(dev_get_drvdata(base),
						  struct rculock_device,
						  base);
	struct rculock_context *ctx;
	unsigned int nr = 0;

	rcu_read_lock();
	ctx = rcu_dereference(dev->head);
	rcu_read_unlock();
	while (ctx) {
		rcu_read_lock();
		ctx = rcu_dereference(ctx->next);
		rcu_read_unlock();
		nr++;
	}
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(active);

static ssize_t free_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct rculock_device *dev = container_of(dev_get_drvdata(base),
						  struct rculock_device,
						  base);
	struct rculock_context *ctx;
	unsigned int nr = 0;

	for (ctx = dev->free; ctx; ctx = ctx->next)
		nr++;
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(free);

static struct attribute *rculock_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_free.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rculock);

static int __init init_driver(struct rculock_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct rculock_driver *drv = &rculock_driver;
	struct rculock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rculock_device *dev;
	char name[9]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		goto err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct rculock_device));
		mutex_init(&dev->lock);
		dev->head = dev->free	= NULL;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.groups	= rculock_groups;
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
	struct rculock_driver *drv = &rculock_driver;
	struct rculock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rculock_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		struct rculock_context *ctx, *next;
		misc_deregister(&dev->base);
		for (ctx = dev->head; ctx; ctx = next) {
			next = ctx->next;
			kfree(ctx);
		}
		for (ctx = dev->free; ctx; ctx = next) {
			next = ctx->next;
			kfree(ctx);
		}
	}
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("RCU lock test module");
