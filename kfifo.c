/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>

struct kfifo_context {
	struct kfifo_context	*next;
	void			*data;
	unsigned int		count;
};

enum kfifo_message_type {
	KFIFO_MESSAGE_TYPE_OPEN,
	KFIFO_MESSAGE_TYPE_RELEASE,
};

struct kfifo_message {
	enum kfifo_message_type	type;
	void			*data;
};

struct kfifo_device {
	struct task_struct	*reader;	/* kfifo consumer */
	atomic_t		alloced;
	atomic_t		proced;
	struct kfifo_context	*head;
	struct kfifo_context	*free;
	struct semaphore	avail;		/* for producer to wakeup */
	struct semaphore	ready;		/* for consumer to wakeup */
	spinlock_t		lock;		/* for multiple producers */
	DECLARE_KFIFO		(fifo, struct kfifo_message, 16);
	struct miscdevice	base;
};

static struct kfifo_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct kfifo_device	devs[2];
} kfifo_driver = {
	.base.name		= "kfifo",
	.base.owner		= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct kfifo_device *dev = container_of(fp->private_data,
						struct kfifo_device, base);
	struct kfifo_message msg;
	int nr;

	/* sleep until queue is available */
	if (down_interruptible(&dev->avail))
		return -ERESTARTSYS;
	/* to guard kfifo from multiple writers */
	msg.type = KFIFO_MESSAGE_TYPE_OPEN;
	msg.data = fp;
	nr = kfifo_in_spinlocked(&dev->fifo, &msg, 1, &dev->lock);
	if (nr != 1) {
		printk(KERN_ERR "[%s:open]: %d=kfifo_in_spinlocked()\n",
		       dev_name(dev->base.this_device), nr);
		return -EINVAL;
	}
	/* notify the reader for the new message */
	up(&dev->ready);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct kfifo_device *dev = container_of(fp->private_data,
						struct kfifo_device, base);
	struct kfifo_message msg;
	int nr;

	/* sleep until queue is available */
	if (down_interruptible(&dev->avail))
		return -ERESTARTSYS;
	/* to guard kfifo from multiple writers */
	msg.type = KFIFO_MESSAGE_TYPE_RELEASE;
	msg.data = fp;
	nr = kfifo_in_spinlocked(&dev->fifo, &msg, 1, &dev->lock);
	if (nr != 1) {
		printk(KERN_ERR "[%s:release]: %d=kfifo_in_spinlocked()\n",
		       dev_name(dev->base.this_device), nr);
		return -EINVAL;
	}
	/* notify the reader for the new message */
	up(&dev->ready);
	return 0;
}

static int kfifo_reader(void *arg)
{
	struct kfifo_device *dev = arg;

	printk(KERN_DEBUG "[%s:reader] started\n",
	       dev_name(dev->base.this_device));
	/* multiple messages at a time */
	while (!kthread_should_stop()) {
		struct kfifo_context **ctxx, *ctx;
		struct kfifo_message msg;
		int nr;
		if (down_interruptible(&dev->ready))
			continue;
		nr = kfifo_out(&dev->fifo, &msg, 1);
		if (!nr) {
			printk(KERN_DEBUG "[%s:reader] no data, retry...\n",
			       dev_name(dev->base.this_device));
			if (msleep_interruptible(10))
				break;
			continue;
		}
		up(&dev->avail);
		atomic_inc(&dev->proced);
		for (ctxx = &dev->head; *ctxx; ctxx = &(*ctxx)->next) {
			ctx = *ctxx;
			if (ctx->data == msg.data) {
				switch (msg.type) {
				case KFIFO_MESSAGE_TYPE_OPEN:
					ctx->count++;
					break;
				case KFIFO_MESSAGE_TYPE_RELEASE:
					ctx->count--;
					if (ctx->count)
						break;
					*ctxx = ctx->next;
					ctx->next = dev->free;
					dev->free = ctx;
					break;
				}
				break;
			}
		}
		if (*ctxx || msg.type == KFIFO_MESSAGE_TYPE_RELEASE)
			continue;
		ctx = dev->free;
		if (ctx)
			dev->free = ctx->next;
		else {
			ctx = kmalloc(sizeof(struct kfifo_context), GFP_KERNEL);
			if (IS_ERR(ctx)) {
				printk(KERN_CRIT "[%s:reader]: out of mem\n",
				       dev_name(dev->base.this_device));
				continue;
			}
			atomic_inc(&dev->alloced);
		}
		ctx->count = 1;
		ctx->next = NULL;
		ctx->data = msg.data;
		*ctxx = ctx;
	}
	printk(KERN_DEBUG "[%s:reader] finished\n",
	       dev_name(dev->base.this_device));
	return 0;
}

static ssize_t active_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	struct kfifo_context *ctx;
	unsigned int nr = 0;

	for (ctx = dev->head; ctx; ctx = ctx->next)
		nr += ctx->count;
	return snprintf(page, PAGE_SIZE, "%d\n", nr);
}
static DEVICE_ATTR_RO(active);

static ssize_t free_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	struct kfifo_context *ctx;
	unsigned int nr = 0;

	for (ctx = dev->free; ctx; ctx = ctx->next)
		nr++;
	return snprintf(page, PAGE_SIZE, "%d\n", nr);
}
static DEVICE_ATTR_RO(free);

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->alloced));
}
static DEVICE_ATTR_RO(alloc);

static ssize_t proc_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&dev->proced));
}
static DEVICE_ATTR_RO(proc);

static struct attribute *kfifo_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_free.attr,
	&dev_attr_alloc.attr,
	&dev_attr_proc.attr,
	NULL,
};

static const struct attribute_group kfifo_group = {
	.attrs	= kfifo_attrs,
};

static ssize_t fifo_initialized_show(struct device *base,
				     struct device_attribute *attr,
				     char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n",
			kfifo_initialized(&dev->fifo) ? 1 : 0);
}
static struct device_attribute fifo_init = __ATTR(initialized, S_IRUGO,
						  fifo_initialized_show,
						  NULL);

static ssize_t fifo_size_show(struct device *base,
			      struct device_attribute *attr,
			      char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", kfifo_size(&dev->fifo));
}
static struct device_attribute fifo_size = __ATTR(size, S_IRUGO,
						  fifo_size_show, NULL);

static ssize_t fifo_esize_show(struct device *base,
			       struct device_attribute *attr,
			       char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", kfifo_esize(&dev->fifo));
}
static struct device_attribute fifo_esize = __ATTR(esize, S_IRUGO,
						   fifo_esize_show, NULL);

static ssize_t fifo_used_show(struct device *base,
			      struct device_attribute *attr,
			      char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", kfifo_len(&dev->fifo));
}
static struct device_attribute fifo_used = __ATTR(used, S_IRUGO,
						  fifo_used_show, NULL);

static ssize_t fifo_avail_show(struct device *base,
			       struct device_attribute *attr,
			       char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", kfifo_avail(&dev->fifo));
}
static struct device_attribute fifo_available = __ATTR(available, S_IRUGO,
						       fifo_avail_show, NULL);

static ssize_t fifo_is_empty_show(struct device *base,
				  struct device_attribute *attr,
				  char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n",
			kfifo_is_empty(&dev->fifo) ? 1 : 0);
}
static struct device_attribute fifo_is_empty = __ATTR(is_empty, S_IRUGO,
						      fifo_is_empty_show,
						      NULL);

static ssize_t fifo_is_full_show(struct device *base,
				 struct device_attribute *attr,
				 char *page)
{
	struct kfifo_device *dev = container_of(dev_get_drvdata(base),
						struct kfifo_device,
						base);
	return snprintf(page, PAGE_SIZE, "%d\n", kfifo_is_full(&dev->fifo));
}
static struct device_attribute fifo_is_full = __ATTR(is_full, S_IRUGO,
						     fifo_is_full_show,
						     NULL);

static struct attribute *kfifo_fifo_attrs[] = {
	&fifo_init.attr,
	&fifo_size.attr,
	&fifo_esize.attr,
	&fifo_used.attr,
	&fifo_available.attr,
	&fifo_is_empty.attr,
	&fifo_is_full.attr,
	NULL,
};

static const struct attribute_group kfifo_fifo_group = {
	.name	= "fifo",
	.attrs	= kfifo_fifo_attrs,
};

static const struct attribute_group *kfifo_groups[] = {
	&kfifo_group,
	&kfifo_fifo_group,
	NULL,
};

static int __init init_driver(struct kfifo_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct kfifo_driver *drv = &kfifo_driver;
	struct kfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct kfifo_device *dev;
	char name[7]; /* strlen(drv->base.name)+2 */
	struct task_struct *p;
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
		memset(dev, 0, sizeof(struct kfifo_device));
		INIT_KFIFO(dev->fifo);
		spin_lock_init(&dev->lock);
		sema_init(&dev->avail, kfifo_size(&dev->fifo));
		sema_init(&dev->ready, 0);
		atomic_set(&dev->alloced, 0);
		atomic_set(&dev->proced, 0);
		dev->head = dev->free	= NULL;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.groups	= kfifo_groups,
		dev->base.minor		= MISC_DYNAMIC_MINOR;
		err = misc_register(&dev->base);
		if (err) {
			end = dev;
			goto err;
		}
		p = kthread_create(kfifo_reader, dev, name);
		if (IS_ERR(p)) {
			end = dev+1;
			err = PTR_ERR(p);
			goto err;
		}
		dev->reader = p;
		wake_up_process(p);
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++) {
		if (dev->reader) {
			/* up to avoid the reader deadlock */
			up(&dev->ready);
			kthread_stop(dev->reader);
		}
		misc_deregister(&dev->base);
	}
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct kfifo_driver *drv = &kfifo_driver;
	struct kfifo_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct kfifo_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		struct kfifo_context *ctx, *next;
		if (dev->reader) {
			/* up to avoid the reader deadlock */
			up(&dev->ready);
			kthread_stop(dev->reader);
		}
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
MODULE_DESCRIPTION("kfifo test module");
