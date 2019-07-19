/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

enum alloc_type {
	ALLOC_TYPE_KMALLOC = 0,
	ALLOC_TYPE_VMALLOC,
	ALLOC_TYPE_KMEMCACHE,
	ALLOC_TYPE_GET_FREE_PAGES,
};

struct alloc_device {
	enum alloc_type	type;
	size_t			alloc;
	struct mutex		lock;
	size_t			size;
	struct kmem_cache	*cache;
	char			*buf;
	struct cdev		cdev;
	struct device		base;
};

static struct alloc_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct alloc_device	devs[4];
} alloc_driver = {
	.base.name		= "alloc",
	.base.owner		= THIS_MODULE,
	.devs[0]	= {
		.type		= ALLOC_TYPE_KMALLOC,
		.alloc		= 16,
		.buf		= NULL,
		.size		= 0,
		.base.init_name	= "alloc16",
	},
	.devs[1]	= {
		.type		= ALLOC_TYPE_VMALLOC,
		.alloc		= 128,
		.buf		= NULL,
		.size		= 0,
		.base.init_name	= "alloc128",
	},
	.devs[2]	= {
		.type		= ALLOC_TYPE_KMEMCACHE,
		.cache		= NULL,
		.alloc		= 256,
		.buf		= NULL,
		.size		= 0,
		.base.init_name	= "alloc256",
	},
	.devs[3]	= {
		.type		= ALLOC_TYPE_GET_FREE_PAGES,
		.alloc		= 4096,	/* must be the power of 2 */
		.buf		= NULL,
		.size		= 0,
		.base.init_name	= "alloc4096",
	},
};

static loff_t llseek(struct file *fp, loff_t offset, int whence)
{
	struct alloc_device *dev = fp->private_data;
	loff_t ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		if (offset == 0) {
			ret = fp->f_pos;
			goto out;
		}
		offset += fp->f_pos;
		break;
	case SEEK_END:
		offset += dev->size;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	if (offset < 0 || offset > dev->alloc) {
		ret = -EINVAL;
		goto out;
	}
	fp->f_pos = offset;
	if (offset > dev->size)
		dev->size = offset;
	ret = offset;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct alloc_device *dev = fp->private_data;
	size_t rem;
	char *ptr;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (count+*pos > dev->size)
		count = dev->size-*pos;
	if (count < 0)
		count = 0;
	ptr = dev->buf+*pos;
	rem = count;
	do {
		size_t nr = copy_to_user(buf, ptr, rem);
		buf += rem-nr;
		ptr += rem-nr;
		rem = nr;
	} while (rem);
	ret = count;
	*pos += count;
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count,
		     loff_t *pos)
{
	struct alloc_device *dev = fp->private_data;
	size_t rem;
	char *ptr;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (count+*pos > dev->alloc)
		count = dev->alloc-*pos;
	if (count < 0) {
		ret = -ENOSPC;
		goto out;
	}
	ptr = dev->buf+*pos;
	rem = count;
	do {
		int nr = copy_from_user(ptr, buf, rem);
		ptr += rem-nr;
		buf += rem-nr;
		rem = nr;
	} while (rem);
	ret = count;
	*pos += count;
	if (*pos > dev->size)
		dev->size = *pos;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct alloc_device *dev = container_of(ip->i_cdev, struct alloc_device,
						cdev);
	fp->private_data = dev;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (fp->f_flags&O_TRUNC)
		dev->size = 0;
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct alloc_device *dev = container_of(base, struct alloc_device,
						base);
	return snprintf(page, PAGE_SIZE, "%ld\n", dev->alloc);
}
static DEVICE_ATTR_RO(alloc);

static ssize_t size_show(struct device *base, struct device_attribute *attr,
			char *page)
{
	struct alloc_device *dev = container_of(base, struct alloc_device,
						base);
	size_t val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->size;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", val);
}
static DEVICE_ATTR_RO(size);

static struct attribute *alloc_attrs[] = {
	&dev_attr_alloc.attr,
	&dev_attr_size.attr,
	NULL,
};
ATTRIBUTE_GROUPS(alloc);

static int init_driver(struct alloc_driver *drv)
{
	int size = ARRAY_SIZE(drv->devs);
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, size, drv->base.name);
	if (err)
		return err;
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.llseek	= llseek;
	drv->fops.read		= read;
	drv->fops.write		= write;
	drv->fops.open		= open;
	return 0;
}

static void *alloc_device_buffer(struct alloc_device *dev)
{
	switch (dev->type) {
	case ALLOC_TYPE_KMALLOC:
		return kmalloc(dev->alloc, GFP_KERNEL);
	case ALLOC_TYPE_VMALLOC:
		return vmalloc(dev->alloc);
	case ALLOC_TYPE_KMEMCACHE:
		if (!IS_ERR_OR_NULL(dev->cache))
			return kmem_cache_alloc(dev->cache, GFP_KERNEL);
		dev->cache = kmem_cache_create(dev->base.init_name,
					       dev->alloc, 0, 0, NULL);
		if (IS_ERR(dev->cache))
			return dev->cache;
		return kmem_cache_alloc(dev->cache, GFP_KERNEL);
	case ALLOC_TYPE_GET_FREE_PAGES:
		return (void *)__get_free_pages(GFP_KERNEL, get_order(dev->alloc));
	default:
		printk(KERN_WARNING "unsupported device type\n");
		return NULL;
	}
}

static void free_device_buffer(const struct alloc_device *dev)
{
	switch (dev->type) {
	case ALLOC_TYPE_KMALLOC:
		kfree(dev->buf);
		break;
	case ALLOC_TYPE_VMALLOC:
		vfree(dev->buf);
		break;
	case ALLOC_TYPE_KMEMCACHE:
		if (IS_ERR_OR_NULL(dev->cache))
			return;
		kmem_cache_free(dev->cache, dev->buf);
		kmem_cache_destroy(dev->cache);
		break;
	case ALLOC_TYPE_GET_FREE_PAGES:
		free_pages((unsigned long)dev->buf, get_order(dev->alloc));
		break;
	}
}

static int __init init(void)
{
	struct alloc_driver *drv = &alloc_driver;
	struct alloc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct alloc_device *dev;
	int err;
	int i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		device_initialize(&dev->base);
		dev->base.groups	= alloc_groups;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		dev->buf = alloc_device_buffer(dev);
		if (IS_ERR_OR_NULL(dev->buf)) {
			err = PTR_ERR(dev->buf);
			end = dev;
			goto err;
		}
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		free_device_buffer(dev);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct alloc_driver *drv = &alloc_driver;
	struct alloc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct alloc_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		free_device_buffer(dev);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Various allocation test module");
