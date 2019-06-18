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
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/timekeeping.h>

static struct jit_driver {
	struct mutex		lock;
	unsigned int		currenttime_max_nr;
	unsigned int		default_busy_wait_msec;
	unsigned int		busy_wait_msec;
	unsigned int		busy_wait_max_nr;
	char			buf[PAGE_SIZE];
	struct proc_dir_entry	*top;
	struct device_driver	base;
	struct seq_operations	sops[2];
	struct file_operations	fops[1];
} jit_driver = {
	.currenttime_max_nr	= 256,	/* 256 max currenttime entries */
	.default_busy_wait_msec	= 1000,	/* 1 sec */
	.busy_wait_max_nr	= 12,	/* 12 max busy waits */
	.top			= NULL,
	.base.owner		= THIS_MODULE,
	.base.name		= "jit",
};

static int show_hz(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", HZ);
	return 0;
}

static int show_user_hz(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", USER_HZ);
	return 0;
}

static void *start_currenttime(struct seq_file *m, loff_t *pos)
{
	struct jit_driver *drv = PDE_DATA(file_inode(m->file));
	if (*pos >= drv->currenttime_max_nr)
		return NULL;
	seq_printf(m, "%-18s %-10s %-18s %s\n%48c %-s\n",
		   "get_cycles()", "jiffies", "jiffies_64",
		   "ktime_get_real_ts64()", ' ', "ktime_get_real_fast_ns()");
	return drv;
}

static void stop_currenttime(struct seq_file *m, void *v)
{
	return;
}

static void *next_currenttime(struct seq_file *m, void *v, loff_t *pos)
{
	struct jit_driver *drv = v;
	if (++(*pos) >= drv->currenttime_max_nr)
		return NULL;
	return v;
}

static int show_currenttime(struct seq_file *m, void *v)
{
	struct timespec64 ts;
	u64 ns;

	ktime_get_real_ts64(&ts);
	ns = ktime_get_real_fast_ns();
	seq_printf(m, "0x%016llx 0x%08lx 0x%016llx " \
		   "%10lld.%09ld\n%48c %10lld.%09lld\n",
		   get_cycles(), jiffies&0xffffffff, get_jiffies_64(),
		   ts.tv_sec, ts.tv_nsec, ' ',
		   ns/NSEC_PER_SEC, ns%NSEC_PER_SEC);
	return 0;
}

static void *start_busy(struct seq_file *m, loff_t *pos)
{
	struct jit_driver *drv = PDE_DATA(file_inode(m->file));
	if (*pos >= drv->busy_wait_max_nr)
		return NULL;
	seq_printf(m, "%9s %9s\n", "start", "end");
	return drv;
}

static void stop_busy(struct seq_file *m, void *v)
{
	return;
}

static void *next_busy(struct seq_file *m, void *v, loff_t *pos)
{
	struct jit_driver *drv = v;
	if (++(*pos) >= drv->busy_wait_max_nr)
		return NULL;
	return v;
}

static int show_busy(struct seq_file *m, void *v)
{
	struct jit_driver *drv = v;
	unsigned long start = jiffies;
	unsigned long end = start+HZ*drv->busy_wait_msec/MSEC_PER_SEC;

	/* busy wait for drv->busy_wait_sec */
	while (time_before(jiffies, end))
		cpu_relax();
	/* 20 bytes */
	seq_printf(m, "%9ld %9ld\n", start&0xffffffff, jiffies&0xffffffff);
	return 0;
}

static int show_busy_wait_msec(struct seq_file *m, void *v)
{
	struct jit_driver *drv = m->private;
	unsigned long msec;

	if (mutex_lock_interruptible(&drv->lock))
		return -ERESTARTSYS;
	msec = drv->busy_wait_msec;
	mutex_unlock(&drv->lock);
	seq_printf(m, "%lu\n", msec);
	return 0;
}

static ssize_t write_busy_wait_msec(struct file *fp, const char __user *buf,
				    size_t count, loff_t *pos)
{
	struct jit_driver *drv = PDE_DATA(file_inode(fp));
	long msec;
	int ret;

	if (mutex_lock_interruptible(&drv->lock))
		return -ERESTARTSYS;
	if (copy_from_user(drv->buf, buf, sizeof(drv->buf)))
		return -EFAULT;
	ret = kstrtol(drv->buf, 10, &msec);
	if (ret)
		goto out;
	/* ignore the out of range values */
	if (msec > 0 && msec <= LONG_MAX)
		drv->busy_wait_msec = msec;
	else if (msec == 0)
		drv->busy_wait_msec = drv->default_busy_wait_msec;
	ret = count;
out:
	mutex_unlock(&drv->lock);
	return ret;
}

static int open_busy_wait_msec(struct inode *ip, struct file *fp)
{
	return single_open(fp, show_busy_wait_msec, PDE_DATA(ip));
}

static int __init init(void)
{
	struct jit_driver *drv = &jit_driver;
	struct file_operations *fops;
	struct seq_operations *sops;
	struct proc_dir_entry *dir;
	char path[11]; /* strlen("driver/")+strlen(drv->base.name) */
	int err;

	mutex_init(&drv->lock);
	drv->busy_wait_msec = drv->default_busy_wait_msec;
	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return err;
	dir = proc_mkdir(path, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	dir = proc_create_single("hz", 0, drv->top, show_hz);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	dir = proc_create_single("user_hz", 0, drv->top, show_user_hz);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	sops		= &drv->sops[0];
	sops->start	= start_currenttime;
	sops->stop	= stop_currenttime;
	sops->next	= next_currenttime;
	sops->show	= show_currenttime;
	dir = proc_create_seq_data("currenttime", 0, drv->top, sops, drv);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	sops		= &drv->sops[1];
	sops->start	= start_busy;
	sops->stop	= stop_busy;
	sops->next	= next_busy;
	sops->show	= show_busy;
	dir = proc_create_seq_data("jitbusy", 0, drv->top, sops, drv);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	fops		= &drv->fops[0];
	fops->owner	= THIS_MODULE,
	fops->read	= seq_read;
	fops->write	= write_busy_wait_msec;
	fops->open	= open_busy_wait_msec;
	fops->release	= single_release;
	dir = proc_create_data("busy_wait_msec", S_IRUGO|S_IWUSR,
			       drv->top, fops, drv);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto err;
	}
	return 0;
err:
	proc_remove(drv->top);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jit_driver *drv = &jit_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Time module");
