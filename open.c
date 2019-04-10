/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/atomic.h>

struct open_device {
	atomic_t	open_nr;
	struct cdev	cdev;
	struct device	base;
};

static struct open_driver {
	const char		*const name;
	dev_t			devt;
	struct device_driver	base;
	struct open_device	devs[2];
} open_driver = {
	.name		= "open",
	.base.owner	= THIS_MODULE,
	.devs		= {
		{.open_nr = ATOMIC_INIT(0)},
		{.open_nr = ATOMIC_INIT(0)},
	},
};

static ssize_t read(struct file *fp, char __user *buf, size_t len, loff_t *pos)
{
	struct open_device *dev = fp->private_data;
	printk("read(open_nr=%d)\n", atomic_read(&dev->open_nr));
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t len, loff_t *pos)
{
	struct open_device *dev = fp->private_data;
	printk("write(open_nr=%d)\n", atomic_read(&dev->open_nr));
	return 0;
}

static int open(struct inode *ip, struct file *fp)
{
	struct open_device *dev = container_of(ip->i_cdev, struct open_device, cdev);
	fp->private_data = dev;
	atomic_inc(&dev->open_nr);
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	struct open_device *dev = fp->private_data;
	atomic_dec(&dev->open_nr);
	return 0;
}

static const struct file_operations open_fops =  {
	.read		= read,
	.write		= write,
	.open		= open,
	.release	= release,
};

static int __init init(void)
{
	struct open_driver *drv = &open_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	struct open_device *dev;
	char init_name[12];
	int err;

	err = alloc_chrdev_region(&drv->devt, 0, nr, drv->name);
	if (err)
		return err;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(init_name, sizeof(init_name), "%s%d", drv->name, i);
		if (!err) {
			j = i;
			goto err;
		}
		dev->base.init_name = init_name;
		dev->base.driver = &drv->base;
		dev->base.devt = MKDEV(MAJOR(drv->devt), MINOR(drv->devt)+i);
		device_initialize(&dev->base);
		dev->cdev.owner = drv->base.owner;
		cdev_init(&dev->cdev, &open_fops);
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
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct open_driver *drv = &open_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct open_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		cdev_device_del(&dev->cdev, &dev->base);
	unregister_chrdev_region(drv->devt, nr);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("open(2) and close(2) example");
