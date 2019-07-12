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
#include <linux/mutex.h>
#include <linux/uaccess.h>

struct lseek_device {
	size_t		alloc;
	struct mutex	lock;
	size_t		len;
	char		*buf;
	struct cdev	cdev;
	struct device	base;
};

static struct lseek_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct lseek_device	devs[4];
} lseek_driver = {
	.base.name		= "lseek",
	.base.owner		= THIS_MODULE,
	.devs[0]	= {
		.alloc		= 16,
		.buf		= NULL,
		.len		= 0,
		.base.init_name	= "lseek16",
	},
	.devs[1]	= {
		.alloc		= 64,
		.buf		= NULL,
		.len		= 0,
		.base.init_name	= "lseek64",
	},
	.devs[2]	= {
		.alloc		= 128,
		.buf		= NULL,
		.len		= 0,
		.base.init_name	= "lseek128",
	},
	.devs[3]	= {
		.alloc		= 256,
		.buf		= NULL,
		.len		= 0,
		.base.init_name	= "lseek256",
	},
};

static loff_t llseek(struct file *fp, loff_t offset, int whence)
{
	struct lseek_device *dev = fp->private_data;
	printk(KERN_DEBUG "llseek[%s]\n", dev_name(&dev->base));
	return 0;
}

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct lseek_device *dev = fp->private_data;
	printk(KERN_DEBUG "read[%s]\n", dev_name(&dev->base));
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count,
		     loff_t *pos)
{
	struct lseek_device *dev = fp->private_data;
	char *ptr;
	int rem;
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
	if (*pos > dev->len)
		dev->len = *pos;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct lseek_device *dev = container_of(ip->i_cdev, struct lseek_device,
						cdev);
	fp->private_data = dev;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (fp->f_flags&O_TRUNC)
		dev->len = 0;
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t alloc_show(struct device *base, struct device_attribute *attr,
			  char *page)
{
	struct lseek_device *dev = container_of(base, struct lseek_device,
						base);
	return snprintf(page, PAGE_SIZE, "%ld\n", dev->alloc);
}
static DEVICE_ATTR_RO(alloc);

static ssize_t len_show(struct device *base, struct device_attribute *attr,
			char *page)
{
	struct lseek_device *dev = container_of(base, struct lseek_device,
						base);
	size_t val;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	val = dev->len;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", val);
}
static DEVICE_ATTR_RO(len);

static struct attribute *lseek_attrs[] = {
	&dev_attr_alloc.attr,
	&dev_attr_len.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lseek);

static int init_driver(struct lseek_driver *drv)
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

static int __init init(void)
{
	struct lseek_driver *drv = &lseek_driver;
	struct lseek_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct lseek_device *dev;
	int err;
	int i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		device_initialize(&dev->base);
		dev->base.groups	= lseek_groups;
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		cdev_init(&dev->cdev, &drv->fops);
		mutex_init(&dev->lock);
		dev->buf = kmalloc(dev->alloc, GFP_KERNEL);
		if (IS_ERR(dev->buf)) {
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
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct lseek_driver *drv = &lseek_driver;
	struct lseek_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct lseek_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		kfree(dev->buf);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("lseek(2) test module");
