/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>

typedef void *scullc_quantum_t;

struct scullc_qset {
	struct scullc_qset	*next;
	scullc_quantum_t	qset[(PAGE_SIZE-sizeof(struct scullc_qset *))
					/sizeof(scullc_quantum_t)];
} ____cacheline_aligned_in_smp;

struct scullc_device {
	struct mutex		lock;
	struct scullc_qset	*qset;
	struct cdev		cdev;
	struct device		base;
};

static struct scullc_driver {
	int			qset_size;
	size_t			quantum_size;
	struct kmem_cache	*quantums;
	struct kmem_cache	*qsets;
	dev_t			devt;
	struct device_driver	base;
	struct file_operations	fops;
	struct scullc_device	devs[2];
} scullc_driver = {
	.qset_size	= sizeof(((struct scullc_qset *)0)->qset)/sizeof(scullc_quantum_t),
	.quantum_size	= PAGE_SIZE,
	.base.name	= "scullc",
	.base.owner	= THIS_MODULE,
};

static struct scullc_qset *follow(struct scullc_device *dev, loff_t pos)
{
	struct scullc_driver *drv = container_of(dev->base.driver,
						 struct scullc_driver,
						 base);
	int i, qset_pos = (pos+1)/(drv->qset_size*drv->quantum_size);
	struct scullc_qset *newp, **qsetp = &dev->qset;

	for (i = 0; i < qset_pos; i++) {
		if (*qsetp) {
			qsetp = &(*qsetp)->next;
			continue;
		}
		newp = kmem_cache_zalloc(drv->qsets, GFP_KERNEL);
		if (!newp)
			return NULL;
		*qsetp = newp;
		qsetp = &newp->next;
	}
	if (!*qsetp)
		*qsetp = kmem_cache_zalloc(drv->qsets, GFP_KERNEL);
	return *qsetp;
}

static void trim(struct scullc_device *dev)
{
	struct scullc_driver *drv = container_of(dev->base.driver,
						 struct scullc_driver,
						 base);
	struct scullc_qset *nextp, *qset;

	for (qset = dev->qset; qset; qset = nextp) {
		nextp = qset->next;
		kmem_cache_free(drv->qsets, qset);
	}
}

static ssize_t read(struct file *fp, char *__user buf, size_t count, loff_t *pos)
{
	struct scullc_device *dev = fp->private_data;
	printk(KERN_DEBUG "read[%s:%s]\n", dev->base.driver->name,
	       dev_name(&dev->base));
	return 0;
}

static ssize_t write(struct file *fp, const char *__user buf, size_t count, loff_t *pos)
{
	struct scullc_device *dev = fp->private_data;
	struct scullc_qset *qset;
	int ret;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	qset = follow(dev, *pos);
	if (!qset) {
		ret = -ENOMEM;
		goto out;
	}
	*pos += count;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scullc_device *dev = container_of(ip->i_cdev,
						 struct scullc_device,
						 cdev);

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	fp->private_data = dev;
	if (fp->f_flags&O_TRUNC)
		trim(dev);
	mutex_unlock(&dev->lock);
	return 0;
}

static ssize_t quantum_size_show(struct device *base,
				 struct device_attribute *attr,
				 char *page)
{
	const struct scullc_driver *drv = container_of(base->driver,
						       struct scullc_driver,
						       base);
	return snprintf(page, PAGE_SIZE, "%ld\n", drv->quantum_size);
}
static DEVICE_ATTR_RO(quantum_size);

static ssize_t qset_size_show(struct device *base,
			      struct device_attribute *attr,
			      char *page)
{
	struct scullc_driver *drv = container_of(base->driver,
						 struct scullc_driver,
						 base);
	return snprintf(page, PAGE_SIZE, "%d\n", drv->qset_size);
}
static DEVICE_ATTR_RO(qset_size);

static ssize_t qset_count_show(struct device *base,
			       struct device_attribute *attr,
			       char *page)
{
	struct scullc_device *dev = container_of(base,
						 struct scullc_device,
						 base);
	struct scullc_qset *qset;
	size_t nr = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	for (qset = dev->qset; qset; qset = qset->next)
		nr++;
	mutex_unlock(&dev->lock);
	return snprintf(page, PAGE_SIZE, "%ld\n", nr);
}
static DEVICE_ATTR_RO(qset_count);

static struct attribute *scullc_attrs[] = {
	&dev_attr_quantum_size.attr,
	&dev_attr_qset_size.attr,
	&dev_attr_qset_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(scullc);

static int init_driver(struct scullc_driver *drv)
{
	struct kmem_cache *cache;
	int err;

	cache = KMEM_CACHE(scullc_qset, 0);
	if (IS_ERR(cache))
		return PTR_ERR(cache);
	drv->qsets = cache;
	cache = kmem_cache_create("scullc_quantum", drv->quantum_size,
				  __alignof__(drv->quantum_size), 0, NULL);
	if (IS_ERR(cache)) {
		kmem_cache_destroy(drv->qsets);
		return PTR_ERR(cache);
	}
	drv->quantums = cache;
	err = alloc_chrdev_region(&drv->devt, 0, ARRAY_SIZE(drv->devs),
				  drv->base.name);
	if (err) {
		kmem_cache_destroy(drv->quantums);
		kmem_cache_destroy(drv->qsets);
		return err;
	}
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner	= drv->base.owner;
	drv->fops.read	= read;
	drv->fops.write	= write;
	drv->fops.open	= open;
	return 0;
}

static int __init init(void)
{
	struct scullc_driver *drv = &scullc_driver;
	struct scullc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullc_device *dev;
	char name[8]; /* strlen(drv->name)+2 */
	int err, i;

	err = init_driver(drv);
	if (err)
		return err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		mutex_init(&dev->lock);
		dev->qset		= NULL;
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->base.init_name	= name;
		dev->base.driver	= &drv->base;
		dev->base.groups	= scullc_groups;
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	return 0;
err:
	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	kmem_cache_destroy(drv->quantums);
	kmem_cache_destroy(drv->qsets);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct scullc_driver *drv = &scullc_driver;
	struct scullc_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct scullc_device *dev;

	for (dev = drv->devs; dev != end; dev++) {
		cdev_device_del(&dev->cdev, &dev->base);
		trim(dev);
	}
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	kmem_cache_destroy(drv->quantums);
	kmem_cache_destroy(drv->qsets);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull backed by lookaside cache");
