/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

struct rculock_context {
	struct rculock_context	*next;
	void			*data;
	unsigned int		count;
};

struct rculock_device {
	struct rculock_context	*head;
	struct rculock_context	*free;
	struct miscdevice	base;
};

static struct rculock_driver {
	struct file_operations	fops;
	struct device_driver	base;
	struct rculock_device	devs[2];
} rculock_driver = {
	.base.name	= "rculock",
	.base.owner	= THIS_MODULE,
};

static int open(struct inode *ip, struct file *fp)
{
	return 0;
}

static int release(struct inode *ip, struct file *fp)
{
	return 0;
}

static ssize_t active_show(struct device *base, struct device_attribute *attr,
			   char *page)
{
	struct rculock_device *dev = container_of(dev_get_drvdata(base),
						  struct rculock_device,
						  base);
	struct rculock_context *ctx;
	unsigned int nr = 0;

	for (ctx = dev->head; ctx; ctx = ctx->next)
		nr++;
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(active);

static ssize_t free_show(struct device *base, struct device_attribute *attr,
			 char *page)
{
	struct rculock_device *dev = container_of(dev_get_drvdata(base),
						  struct rculock_device,
						  base);
	struct rculock_context *ctx;
	unsigned int nr = 0;

	for (ctx = dev->free; ctx; ctx = ctx->next)
		nr++;
	return snprintf(page, PAGE_SIZE, "%u\n", nr);
}
static DEVICE_ATTR_RO(free);

static struct attribute *rculock_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_free.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rculock);

static int __init init_driver(struct rculock_driver *drv)
{
	memset(&drv->fops, 0, sizeof(struct file_operations));
	drv->fops.owner		= drv->base.owner;
	drv->fops.open		= open;
	drv->fops.release	= release;
	return 0;
}

static int __init init(void)
{
	struct rculock_driver *drv = &rculock_driver;
	struct rculock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rculock_device *dev;
	char name[9]; /* strlen(drv->base.name)+2 */
	int i, err;

	err = init_driver(drv);
	if (err)
		goto err;
	for (dev = drv->devs, i = 0; dev != end; dev++, i++) {
		err = snprintf(name, sizeof(name), "%s%d", drv->base.name, i);
		if (err < 0) {
			end = dev;
			goto err;
		}
		memset(dev, 0, sizeof(struct rculock_device));
		dev->head = dev->free	= NULL;
		dev->base.name		= name;
		dev->base.fops		= &drv->fops;
		dev->base.groups	= rculock_groups;
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
	struct rculock_driver *drv = &rculock_driver;
	struct rculock_device *end = drv->devs+ARRAY_SIZE(drv->devs);
	struct rculock_device *dev;

	for (dev = drv->devs; dev != end; dev++)
		misc_deregister(&dev->base);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("RCU lock test module");



