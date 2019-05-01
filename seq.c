/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct seq_device {
	struct mutex		lock;
	void			*data;
	size_t			alloc;
	size_t			size;
	struct miscdevice	base;
};

static struct seq_driver {
	struct proc_dir_entry	*top;
	struct file_operations	fops;
	struct seq_operations	dops;
	struct device_driver	base;
	struct seq_device	devs[8];
} seq_driver = {
	.top		= NULL,
	.base.owner	= THIS_MODULE,
	.base.name	= "seq",
};

static ssize_t read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct seq_device *dev = container_of(fp->private_data, struct seq_device, base);
	ssize_t ret = -EINVAL;
	size_t rem, len;
	char *ptr;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (*pos > dev->size) {
		ret = 0;
		goto out;
	}
	if (*pos+count > dev->size)
		count = dev->size-*pos;
	ptr = (char *)dev->data+*pos;
	len = count;
	while ((rem = copy_to_user(buf, ptr, len))) {
		buf += len-rem;
		ptr += len-rem;
		len = rem;
	}
	*pos += count;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct seq_device *dev = container_of(fp->private_data, struct seq_device, base);
	ssize_t ret = -ENOMEM;
	size_t rem, len;
	char *ptr;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if ((*pos+count) > dev->alloc) {
		/* make buffer multiple of pages */
		size_t alloc = ((*pos+count)/PAGE_SIZE+1)*PAGE_SIZE;
		ptr = kzalloc(alloc, GFP_KERNEL);
		if (IS_ERR(ptr)) {
			ret = PTR_ERR(ptr);
			goto out;
		}
		if (dev->data) {
			memcpy(ptr, dev->data, dev->size);
			kfree(dev->data);
		}
		dev->alloc = alloc;
		dev->data = ptr;
	}
	ptr = dev->data+*pos;
	len = count;
	while ((rem = copy_from_user(ptr, buf, len))) {
		ptr += len-rem;
		buf += len-rem;
		len = rem;
	}
	*pos += count;
	if (*pos > dev->size)
		dev->size = *pos;
	ret = count;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static void *start_device(struct seq_file *m, loff_t *pos)
{
	struct seq_driver *drv = PDE_DATA(file_inode(m->file));
	int nr = ARRAY_SIZE(drv->devs);
	if (*pos >= nr)
		return NULL;
	m->private = drv;
	return &drv->devs[*pos];
}

static void stop_device(struct seq_file *m, void *v)
{
	return;
}

static void *next_device(struct seq_file *m, void *v, loff_t *pos)
{
	struct seq_driver *drv = m->private;
	int nr = ARRAY_SIZE(drv->devs);
	if (++(*pos) >= nr)
		return NULL;
	return &drv->devs[*pos];
}

static int show_device(struct seq_file *m, void *v)
{
	struct seq_device *dev = v;
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	seq_printf(m, "Name: %s\n", dev_name(dev->base.this_device));
	seq_printf(m, "\tData pointer:\t\t%-p\n\tData size:\t\t%-ld\n",
		   dev->data, dev->size);
	seq_printf(m, "\tData buffer size:\t%-ld\n", dev->alloc);
	mutex_unlock(&dev->lock);
	return 0;
}

static void __init init_driver(struct seq_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner = THIS_MODULE;
	drv->fops.read	= read;
	drv->fops.write	= write;
	memset(&drv->dops, 0, sizeof(struct seq_operations));
	drv->dops.start	= start_device;
	drv->dops.stop	= stop_device;
	drv->dops.next	= next_device;
	drv->dops.show	= show_device;
}

static int __init init_proc(struct seq_driver *drv)
{
	struct proc_dir_entry *dir, *parent;
	char path[11];
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return err;
	dir = proc_mkdir(path, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	dir = proc_mkdir("devices", drv->top);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	parent = dir;
	dir = proc_create_seq_data("all", S_IRUGO, parent, &drv->dops, drv);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	return 0;
err:
	proc_remove(drv->top);
	return err;
}

static int __init init(void)
{
	struct seq_driver *drv = &seq_driver;
	struct seq_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seq_device *dev;
	char name[5]; /* sizeof(drv->base.name)+2 */
	int i, err;

	init_driver(drv);
	for (dev = drv->devs, i = 0; dev < end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		mutex_init(&dev->lock);
		dev->alloc	= 0;
		dev->size	= 0;
		dev->data	= NULL;
		dev->base.name	= name;
		dev->base.fops	= &drv->fops;
		dev->base.minor	= MISC_DYNAMIC_MINOR;
		err = misc_register(&dev->base);
		if (err) {
			end = dev;
			goto err;
		}
	}
	err = init_proc(drv);
	if (err)
		goto err;
	return 0;
err:
	for (dev = drv->devs; dev < end; dev++)
		misc_deregister(&dev->base);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct seq_driver *drv = &seq_driver;
	struct seq_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct seq_device *dev;

	for (dev = drv->devs; dev < end; dev++) {
		misc_deregister(&dev->base);
		kfree(dev->data);
	}
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/seq_file.h> example");
