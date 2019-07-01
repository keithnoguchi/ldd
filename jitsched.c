/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/param.h>
#include <linux/time64.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

static struct jitsched_driver {
	struct mutex		lock;
	unsigned int		delay_ms;
	char			buf[NAME_MAX];
	struct proc_dir_entry	*proc;
	const unsigned int	max_retry;
	const unsigned int	default_delay_ms;
	const char		*const name;
	struct seq_operations	sops[1];
	struct file_operations	fops[1];
} jitsched_drivers[] = {
	{
		.max_retry		= 12,	/* max retry */
		.default_delay_ms	= 1000,	/* 1 sec */
		.name			= "jitsched",
	},
	{
		.max_retry		= 12,	/* max retry */
		.default_delay_ms	= 1000,	/* 1 sec */
		.name			= "jitschedto",
	},
};

static void *start(struct seq_file *m, loff_t *pos)
{
	struct jitsched_driver *drv = PDE_DATA(file_inode(m->file));
	if (*pos >= drv->max_retry)
		return NULL;
	seq_printf(m, "%9s %9s\n", "before", "after");
	return drv;
}

static void stop(struct seq_file *m, void *v)
{
	return;
}

static void *next(struct seq_file *m, void *v, loff_t *pos)
{
	struct jitsched_driver *drv = v;
	if (++(*pos) >= drv->max_retry)
		return NULL;
	return v;
}

static int show(struct seq_file *m, void *v)
{
	struct jitsched_driver *drv = v;
	unsigned long start = jiffies;
	unsigned long end = start+HZ*drv->delay_ms/MSEC_PER_SEC;
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
		drv->delay_ms = drv->default_delay_ms;
	else
		drv->delay_ms = ms;
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
	struct jitsched_driver *drv = jitsched_drivers;
	struct jitsched_driver *end = drv+ARRAY_SIZE(jitsched_drivers);
	struct file_operations *fops = drv->fops;
	struct seq_operations *sops = drv->sops;
	struct proc_dir_entry *proc;
	char path[18]; /* strlen("driver/")+strlen(drv->name)+1 */
	int err;

	for (drv = jitsched_drivers; drv != end; drv++) {
		err = snprintf(path, sizeof(path), "driver/%s", drv->name);
		if (err < 0) {
			end = drv;
			goto err;
		}
		mutex_init(&drv->lock);
		drv->delay_ms	= drv->default_delay_ms;
		sops->start	= start;
		sops->stop	= stop;
		sops->next	= next;
		sops->show	= show;
		fops->owner	= THIS_MODULE;
		fops->read	= seq_read;
		fops->write	= write;
		fops->open	= open;
		fops->release	= seq_release;
		proc = proc_create_data(path, S_IRUGO|S_IWUSR, NULL, fops, drv);
		if (IS_ERR(proc)) {
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc	= proc;
	}
	return 0;
err:
	for (drv = jitsched_drivers; drv != end; drv++)
		proc_remove(drv->proc);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jitsched_driver *drv = jitsched_drivers;
	struct jitsched_driver *end = drv+ARRAY_SIZE(jitsched_drivers);

	for (drv = jitsched_drivers; drv != end; drv++)
		proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Time schedule based delay module");
