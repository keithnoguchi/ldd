/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>

static struct jitsched_driver {
	struct mutex		lock;
	unsigned int		default_wait_ms;
	unsigned int		wait_ms;
	unsigned int		wait_max_nr;
	char			buf[PAGE_SIZE];
	struct proc_dir_entry	*top;
	const char		*const name;
	struct seq_operations	sops[1];
	struct file_operations	fops[1];
} jitsched_driver = {
	.default_wait_ms	= 1000,	/* 1 sec */
	.wait_max_nr		= 12,	/* 12 max sched waits */
	.top			= NULL,
	.name			= "jitsched",
};

static void *start(struct seq_file *m, loff_t *pos)
{
	struct jitsched_driver *drv = PDE_DATA(file_inode(m->file));
	if (*pos >= drv->wait_max_nr)
		return NULL;
	seq_printf(m, "%9s %9s\n", "start", "end");
	return drv;
}

static void stop(struct seq_file *m, void *v)
{
	return;
}

static void *next(struct seq_file *m, void *v, loff_t *pos)
{
	struct jitsched_driver *drv = v;
	if (++(*pos) >= drv->wait_max_nr)
		return NULL;
	return v;
}

static int show(struct seq_file *m, void *v)
{
	struct jitsched_driver *drv = v;
	unsigned long start = jiffies;
	unsigned long end = start+HZ*drv->wait_ms/MSEC_PER_SEC;
	while (time_before(jiffies, end))
		schedule();
	seq_printf(m, "%9ld %9ld\n", start&0xffffffff, jiffies&0xffffffff);
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count,
		     loff_t *pos)
{
	struct jitsched_driver *drv = PDE_DATA(file_inode(fp));
	long ms;
	int ret;

	if (mutex_lock_interruptible(&drv->lock))
		return -ERESTARTSYS;
	if (copy_from_user(drv->buf, buf, sizeof(drv->buf))) {
		ret = -EFAULT;
		goto out;
	}
	ret = kstrtol(drv->buf, 10, &ms);
	if (ret)
		goto out;
	/* ignore the out of range values */
	if (ms < 0 || ms > LONG_MAX) {
		ret = -EINVAL;
		goto out;
	}
	if (ms == 0)
		drv->wait_ms = drv->default_wait_ms;
	else
		drv->wait_ms = ms;
	ret = count;
out:
	mutex_unlock(&drv->lock);
	return ret;
}

static int open(struct inode *ip, struct file *fp)
{
	struct jitsched_driver *drv = PDE_DATA(ip);
	return seq_open(fp, drv->sops);
}

static int __init init(void)
{
	struct jitsched_driver *drv = &jitsched_driver;
	struct file_operations *fops = drv->fops;
	struct seq_operations *sops = drv->sops;
	struct proc_dir_entry *top;
	char path[16]; /* strlen("driver/")+strlen(drv->name)+1 */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->name);
	if (err < 0)
		return err;
	sops->start	= start;
	sops->stop	= stop;
	sops->next	= next;
	sops->show	= show;
	fops->owner	= THIS_MODULE,
	fops->read	= seq_read;
	fops->write	= write;
	fops->open	= open;
	fops->release	= seq_release;
	drv->wait_ms	= drv->default_wait_ms;
	top = proc_create_data(path, S_IRUGO|S_IWUSR, NULL, fops, drv);
	if (IS_ERR(top))
		return PTR_ERR(top);
	mutex_init(&drv->lock);
	drv->top = top;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct jitsched_driver *drv = &jitsched_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Time scheduled wait module");
