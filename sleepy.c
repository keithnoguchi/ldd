/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

/* sleep device file operations */
static ssize_t sleepy_read(struct file *f, char __user *buf, size_t nr, loff_t *pos)
{
	return 0;
}

static ssize_t sleepy_write(struct file *f, const char __user *buf, size_t nr, loff_t *pos)
{
	return 0;
}

static int sleepy_open(struct inode *i, struct file *f)
{
	return 0;
}

static int sleepy_release(struct inode *i, struct file *f)
{
	return 0;
}

static const struct file_operations sleepy_fops = {
	.read		= sleepy_read,
	.write		= sleepy_write,
	.open		= sleepy_open,
	.release	= sleepy_release,
};

/* miscdevice based sleepy device */
static struct miscdevice sleepy_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "sleepy",
	.fops	= &sleepy_fops,
};

int sleepy_register(void)
{
	return misc_register(&sleepy_device);
}

void sleepy_unregister(void)
{
	misc_deregister(&sleepy_device);
}
