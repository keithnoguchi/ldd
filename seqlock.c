/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/seqlock.h>

struct seqlock_context {
	struct seqlock_context	*next;
	void			*data;
	unsigned int		count;
};

struct seqlock_device {
	seqlock_t		lock;
	unsigned int		actives;
	unsigned int		frees;
	struct seqlock_context	*head;
	struct seqlock_context	*free;
	struct miscdevice	base;
};

static struct seqlock_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct seqlock_device	devs[2];
} seqlock_driver = {
	.base.name	= "seqlock",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct seqlock_device *dev = container_of(fp->private_data,
						  struct seqlock_device,
						  base);
	struct seqlock_context **ctxx, *ctx;
	int err = 0;

	if ((fp->f_flags&O_ACCMODE) == O_RDONLY) {
		unsigned int seq, actives, frees;
		/* keep retrying to get the number of writers until
		 * we won't have any concurrent writers */
		do {
			seq = read_seqbegin(&dev->lock);
			actives	= dev->actives;
			frees	= dev->frees;
		} while (read_seqretry(&dev->lock, seq));
		return 0;
	}
	write_seqlock(&dev->lock);
	for (ctxx = &dev->head; *ctxx; ctxx = &(*ctxx)->next)
		if ((*ctxx)->data == fp) {
			(*ctxx)->count++;
			goto out;
		}
	/* new entry */
	ctx = dev->free;
	if (ctx) {
		dev->free = ctx->next;
		dev->frees--;
	} else {
		ctx = kmalloc(sizeof(struct seqlock_device), GFP_KERNEL);
		if (IS_ERR(ctx)) {
			err = PTR_ERR(ctx);
			goto out;
		}
	}
	ctx->count = 1;
	ctx->next = NULL;
	ctx->data = fp;
	*ctxx = ctx;
out:
	dev->actives++;
	write_sequnlock(&dev->lock);
	return err;
}

static int release(struct inode *ip, struct file *fp)
{
	struct seqlock_device *dev = container_of(fp->private_data,
						  struct seqlock_device,
						  base);
	struct seqlock_context **ctxx, *ctx;
	int err = 0;

	if ((fp->f_flags&O_ACCMODE) == O_RDONLY) {
		unsigned int seq, actives, frees;
		do {
			seq = read_seqbegin(&dev->lock);
			actives	= dev->actives;
			frees	= dev->frees;
		} while (read_seqretry(&dev->lock, seq));
		return 0;
	}
	write_seqlock(&dev->lock);
	for (ctxx = &dev->head; *ctxx; ctxx = &(*ctxx)->next)
		if ((*ctxx)->data == fp) {
			ctx = *ctxx;
			dev->actives--;
			if (!--ctx->count) {
				*ctxx = ctx->next;
				ctx->next = dev->free;
				dev->free = ctx;
				dev->frees++;
			}
			goto out;
		}
	/* something wrong if there is no entry found */
	err = -EINVAL;
out:
	write_sequnlock(&dev->lock);
	return err;
}

static ssize_t active_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct seqlock_device *dev = container_of(dev_get_drvdata(base),
						  struct seqlock_device,
						  base);
	unsigned int seq, nr;
	do {
		seq = read_seqbegin(&dev->lock);
		nr = dev->actives;
	} while (read_seqretry(&dev->lock, seq));
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(active);

static ssize_t free_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct seqlock_device *dev = container_of(dev_get_drvdata(base),
						  struct seqlock_device,
						  base);
	unsigned int seq, nr;
	do {
		seq = read_seqbegin(&dev->lock);
		nr = dev->frees;
	} while (read_seqretry(&dev->lock, seq));
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(free);

static struct attribute *seqlock_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_free.attr,
	NULL,
};
ATTRIBUTE_GROUPS(seqlock);

static int __init init_driver(struct seqlock_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct seqlock_driver *drv = &seqlock_driver;
	struct seqlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seqlock_device *dev;
	char name[9]; /* strlen(drv->base.name)+2 */
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
		memset(dev, 0, sizeof(struct seqlock_device));
		seqlock_init(&dev->lock);
		dev->head = dev->free	= NULL;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.groups	= seqlock_groups;
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
	struct seqlock_driver *drv = &seqlock_driver;
	struct seqlock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seqlock_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		struct seqlock_context *ctx, *next;
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
MODULE_DESCRIPTION("Sequence lock test module");
