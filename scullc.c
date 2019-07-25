/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/slab.h>

typedef void *scullc_quntum_t;

struct scullc_qset {
	struct scullc_qset	*next;
	scullc_quntum_t		qset[(PAGE_SIZE-sizeof(struct scullc_qset *))
					/sizeof(scullc_quntum_t)];
} ____cacheline_aligned_in_smp;

struct scullc_device {
	struct cdev	cdev;
	struct device	base;
};

static struct scullc_driver {
	size_t			quantum_size;
	struct kmem_cache	*quantums;
	struct kmem_cache	*qsets;
	dev_t			devt;
	struct device_driver	base;
	struct file_operations	fops;
	struct scullc_device	devs[2];
} scullc_driver = {
	.quantum_size	= PAGE_SIZE,
	.base.name	= "scullc",
	.base.owner	= THIS_MODULE,
};

static ssize_t write(struct file *fp, const char *__user buf, size_t count, loff_t *pos)
{
	*pos += count;
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct scullc_device *dev = container_of(ip->i_cdev,
						 struct scullc_device,
						 cdev);
	printk(KERN_DEBUG "open[%s:%s]\n", dev->base.driver->name,
	       dev_name(&dev->base));
	return 0;
}

static int init_driver(struct scullc_driver *drv)
{
	struct kmem_cache *cache;
	int err;

	cache = KMEM_CACHE(scullc_qset, SLAB_POISON);
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
		cdev_init(&dev->cdev, &drv->fops);
		device_initialize(&dev->base);
		dev->base.devt		= MKDEV(MAJOR(drv->devt),
						MINOR(drv->devt)+i);
		dev->base.init_name	= name;
		dev->base.driver	= &drv->base;
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

	for (dev = drv->devs; dev != end; dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, ARRAY_SIZE(drv->devs));
	kmem_cache_destroy(drv->quantums);
	kmem_cache_destroy(drv->qsets);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Scull backed by lookaside cache");
