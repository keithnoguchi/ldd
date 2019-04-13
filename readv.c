/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fcntl.h>

struct readv_device {
	struct cdev	cdev;
	struct device	base;
};

struct readv_driver {
	dev_t			devt;
	struct file_operations	fops;
	struct device_driver	base;
	struct readv_device	devs[10];
} readv_driver = {
	.base.name	= "readv",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	struct readv_device *dev = container_of(ip->i_cdev, struct readv_device, cdev);
	fp->private_data = dev;

	if ((fp->f_flags&O_ACCMODE) == O_WRONLY)
		return -EINVAL;
	return 0;
}

static void __init init_driver(struct readv_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.open	= open;
}

static int __init init(void)
{
	struct readv_driver *drv = &readv_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	char name[7]; /* 10 devices */
	struct readv_device *dev;
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, nr, drv->base.name);
	if (err)
		return err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
		memset(&dev->base, 0, sizeof(struct device));
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		dev->base.init_name = name;
		dev->base.driver = &drv->base;
		device_initialize(&dev->base);
		cdev_init(&dev->cdev, &drv->fops);
		err = cdev_device_add(&dev->cdev, &dev->base);
		if (err) {
			j = i;
			goto err;
		}
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, nr);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct readv_driver *drv = &readv_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct readv_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("readv(2) example");
