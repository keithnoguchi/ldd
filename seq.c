/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/err.h>

struct seq_device {
	struct cdev		cdev;
	struct miscdevice	base;
};

static struct seq_driver {
	struct proc_dir_entry	*top;
	struct file_operations	fops;
	struct device_driver	base;
	struct seq_device	devs[2];
} seq_driver = {
	.top		= NULL,
	.base.owner	= THIS_MODULE,
	.base.name	= "seq",
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct seq_device *dev = container_of(fp->private_data, struct seq_device, base);
	printk(KERN_ALERT "read(%s)\n", dev_name(dev->base.this_device));
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct seq_device *dev = container_of(fp->private_data, struct seq_device, base);
	printk(KERN_ALERT "write(%s)\n", dev_name(dev->base.this_device));
	return 0;
}

static void __init init_driver(struct seq_driver *drv)
{
	drv->fops.owner = THIS_MODULE;
	drv->fops.read	= read;
	drv->fops.write	= write;
}

static int __init init_proc(struct seq_driver *drv)
{
	struct proc_dir_entry *dir;
	char path[11];
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return err;
	dir = proc_mkdir(path, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	return 0;
}

static int __init init(void)
{
	struct seq_driver *drv = &seq_driver;
	int i, j, nr = ARRAY_SIZE(drv->devs);
	char name[5]; /* max 10 devices */
	struct seq_device *dev;
	int err;

	init_driver(drv);
	for (i = 0, dev = drv->devs; i < nr; i++, dev++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			j = i;
			goto err;
		}
		dev->base.minor = MISC_DYNAMIC_MINOR;
		dev->base.name = name;
		dev->base.fops = &drv->fops;
		err = misc_register(&dev->base);
		if (err) {
			j = i;
			goto err;
		}
	}
	err = init_proc(drv);
	if (err) {
		j = nr;
		goto err;
	}
	return 0;
err:
	for (i = 0, dev = drv->devs; i < j; i++, dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct seq_driver *drv = &seq_driver;
	int i, nr = ARRAY_SIZE(drv->devs);
	struct seq_device *dev;

	for (i = 0, dev = drv->devs; i < nr; i++, dev++)
		misc_deregister(&dev->base);
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/seq_file.h> example");
