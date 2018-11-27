/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* Scull driver */
static struct scull_driver {
	size_t			qset;
	size_t			quantum;
	struct device_driver	drv;
} scull_driver = {
	.qset		= 1024,
	.quantum	= PAGE_SIZE,
	.drv.name	= "scull",
};

/* Scull devices */
static struct scull_device {
	struct semaphore	sem;
	size_t			size;
	size_t			bufsiz;
	size_t			qset;
	size_t			quantum;
	struct scull_qset	*data;
	struct device		dev;
	struct cdev		cdev;
} devices[] = {
	{
		.dev.init_name	= "scull0",
	},
	{},	/* sentry */
};

/* Scull quantum set */
struct scull_qset {
	struct scull_qset	*next;
	void			**data;
};

/* scull_new allocates a new scull_qset. */
static struct scull_qset *scull_new(struct scull_device *d)
{
	struct scull_qset *qptr;
	void **data;

	qptr = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
	if (qptr == NULL)
		return NULL;
	data = kmalloc(sizeof(void *)*d->qset, GFP_KERNEL);
	if (data == NULL) {
		kfree(qptr);
		return NULL;
	}
	memset(data, 0, sizeof(void *)*d->qset);
	qptr->data = data;
	qptr->next = NULL;

	return qptr;
}

/* scull_trim trims the data.  The device should be locked by the caller. */
static void scull_trim(struct scull_device *d)
{
	struct scull_qset *qset, *next;
	int i;

	for (qset = d->data; qset; qset = next) {
		next = qset->next;
		if (qset->data) {
			for (i = 0; i < d->qset; i++)
				kfree(qset->data[i]);
			kfree(qset->data);
		}
		kfree(qset);
	}
	d->data = NULL;
	d->bufsiz = 0;
	d->size = 0;
}

/* scull_lookup looks up the quantum for the read operation.
 * The device should be locked by the caller. */
static void *scull_lookup(struct scull_device *d, loff_t pos)
{
	loff_t ssize = d->qset*d->quantum;
	struct scull_qset *qptr;
	loff_t spos, qpos;
	int i;

	/* find the quantum set */
	qptr = d->data;
	spos = pos/ssize;
	for (i = 0; i < spos; i++) {
		qptr = qptr->next;
		if (qptr == NULL)
			return NULL;
	}
	if (qptr == NULL || qptr->data == NULL)
		return NULL;

	/* find the quantum */
	qpos = (pos%ssize)/d->quantum;
	return qptr->data[qpos];
}

/* scull_get gets the quantum for write operation.
 * The device should be locked by the caller. */
static void *scull_get(struct scull_device *d, loff_t pos)
{
	loff_t ssize = d->qset*d->quantum;
	struct scull_qset **qptr;
	loff_t spos, qpos;
	int i;

	/* Lookup the qset */
	spos = pos/ssize;
	for (i = 0, qptr = &d->data; i < spos; i++, qptr = &(*qptr)->next) {
		if (*qptr == NULL)
			if ((*qptr = scull_new(d)) == NULL)
				return NULL;
	}
	if (*qptr == NULL)
		if ((*qptr = scull_new(d)) == NULL)
			return NULL;

	/* Lookup the quantum */
	qpos = (pos%ssize)/d->quantum;
	if ((*qptr)->data[qpos] == NULL)
		if (((*qptr)->data[qpos] = kmalloc(d->quantum, GFP_KERNEL)))
			d->bufsiz += d->quantum;
	return (*qptr)->data[qpos];
}

/* Scull device attributes */
static ssize_t pagesize_show(struct device *dev, struct device_attribute *attr,
			     char *page)
{
	return snprintf(page, PAGE_SIZE, "%ld\n", PAGE_SIZE);
}
static DEVICE_ATTR_RO(pagesize);

static ssize_t quantum_set_show(struct device *dev, struct device_attribute *attr,
				char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->qset);
}
static DEVICE_ATTR_RO(quantum_set);

static ssize_t quantum_show(struct device *dev, struct device_attribute *attr,
			    char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->quantum);
}
static DEVICE_ATTR_RO(quantum);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->size);
}
static DEVICE_ATTR_RO(size);

static ssize_t buffer_size_show(struct device *dev, struct device_attribute *attr,
				char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%ld\n", d->bufsiz);
}
static struct device_attribute dev_attr_buffer_size = {
	.attr.name	= "size",
	.attr.mode	= 0444,
	.show		= buffer_size_show,
};

static ssize_t buffer_pointer_show(struct device *dev, struct device_attribute *attr,
			   char *page)
{
	struct scull_device *d = container_of(dev, struct scull_device, dev);
	return snprintf(page, PAGE_SIZE, "%p\n", d->data);
}
static struct device_attribute dev_attr_buffer_pointer = {
	.attr.name	= "pointer",
	.attr.mode	= 0444,
	.show		= buffer_pointer_show,
};

static struct attribute *scull_attrs[] = {
	&dev_attr_pagesize.attr,
	&dev_attr_quantum_set.attr,
	&dev_attr_quantum.attr,
	&dev_attr_size.attr,
	NULL,
};
static struct attribute *scull_buffer_attrs[] = {
	&dev_attr_buffer_size.attr,
	&dev_attr_buffer_pointer.attr,
	NULL,
};
static const struct attribute_group scull_group = {
	.attrs = scull_attrs,
};
static const struct attribute_group scull_buffer_group = {
	.name	= "buffer",
	.attrs	= scull_buffer_attrs,
};
static const struct attribute_group *scull_groups[] = {
	&scull_group,
	&scull_buffer_group,
	NULL,
};

/* Scull device type to carry the device attributes. */
static struct device_type device_type = {
	.name	= "scull",
	.groups	= scull_groups,
};

/* Scull device file operations */
static loff_t scull_llseek(struct file *f, loff_t offset, int whence)
{
	struct scull_device *d = f->private_data;
	loff_t pos;

	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	pos = f->f_pos;
	switch (whence) {
	case SEEK_SET:
		pos = offset;
		break;
	case SEEK_CUR:
		pos = f->f_pos + offset;
	case SEEK_END:
		pos = d->size + offset;
		break;
	}
	f->f_pos = pos;
	up(&d->sem);
	return pos;
}

static ssize_t scull_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct scull_device *d = f->private_data;
	void *dptr;
	loff_t dpos;
	ssize_t ret;

	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	if (*pos+len > d->size)
		len = d->size-*pos;
	/* support per quantum read only */
	dpos = *pos%d->quantum;
	if (dpos+len > d->quantum)
		len = d->quantum-dpos;
	ret = 0;
	if (len == 0)
		goto out;
	ret = -EINVAL;
	dptr = scull_lookup(d, *pos);
	if (dptr == NULL)
		goto out;
	ret = copy_to_user(buf, dptr+dpos, len);
	if (unlikely(ret))
		len -= ret;
	*pos += len;
	ret = len;
out:
	up(&d->sem);
	return ret;
}

static ssize_t scull_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	struct scull_device *d = f->private_data;
	void *dptr;
	loff_t dpos;
	ssize_t ret;

	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	/* find the quantum */
	ret = -ENOMEM;
	dptr = scull_get(d, *pos);
	if (dptr == NULL)
		goto out;
	/* support per quantum write only */
	dpos = *pos%d->quantum;
	if (dpos+len > d->quantum)
		len = d->quantum-dpos;
	ret = 0;
	if (len == 0)
		goto out;
	ret = copy_from_user(dptr+dpos, buf, len);
	if (unlikely(ret))
		len -= ret;
	ret = len;
	*pos += len;
	if (*pos > d->size)
		d->size = *pos;
out:
	up(&d->sem);
	return ret;
}

static int scull_open(struct inode *i, struct file *f)
{
	struct scull_device *d = container_of(i->i_cdev, struct scull_device, cdev);

	f->private_data = d;
	if (down_interruptible(&d->sem))
		return -ERESTARTSYS;
	/* reset the file size when it's opened for write only */
	if ((f->f_flags&O_ACCMODE) == O_WRONLY)
		scull_trim(d);
	up(&d->sem);
	return 0;
}

static int scull_release(struct inode *i, struct file *f)
{
	return 0;
}

static const struct file_operations scull_fops = {
	.llseek		= scull_llseek,
	.read		= scull_read,
	.write		= scull_write,
	.open		= scull_open,
	.release	= scull_release,
};

/* Scull registration */
int __init scull_register(void)
{
	struct scull_device *d, *d_err = NULL;
	dev_t devt;
	int err;
	int i;

	/* allocate scull device region */
	err = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(devices),
				  scull_driver.drv.name);
	if (err)
		return err;

	/* create devices */
	for (d = devices, i = 0; d->dev.init_name; d++, i++) {
		sema_init(&d->sem, 1);
		d->qset = scull_driver.qset;
		d->quantum = scull_driver.quantum;
		d->dev.driver = &scull_driver.drv;
		d->dev.type = &device_type;
		d->dev.devt = MKDEV(MAJOR(devt), MINOR(devt)+i);
		device_initialize(&d->dev);
		cdev_init(&d->cdev, &scull_fops);
		err = cdev_device_add(&d->cdev, &d->dev);
		if (err) {
			d_err = d;
			goto out;
		}
	}
	return 0;
out:
	for (d = devices; d != d_err; d++)
		cdev_device_del(&d->cdev, &d->dev);
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
	return err;
}

void scull_unregister(void)
{
	struct scull_device *d;

	for (d = devices; dev_name(&d->dev); d++) {
		cdev_device_del(&d->cdev, &d->dev);
		scull_trim(d);
	}
	unregister_chrdev_region(devices[0].dev.devt, ARRAY_SIZE(devices));
}
