/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timer.h>

static struct jitimer_driver {
	unsigned long		delay;
	struct proc_dir_entry	*proc;
	const char		*const name;
	const unsigned int	retry_nr;
	const unsigned int	default_delay_ms;
	struct file_operations	fops[1];
} jitimer_driver = {
	.retry_nr		= 5,	/* 5 retry */
	.default_delay_ms	= 10,	/* 10ms */
	.name			= "jitimer",
};

static int show(struct seq_file *m, void *v)
{
	struct jitimer_driver *drv = m->private;
	printk(KERN_DEBUG "hello from %s\n", drv->name);
	seq_printf(m, "hello from %s\n", drv->name);
	return 0;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct jitimer_driver *drv = PDE_DATA(ip);
	return single_open(fp, show, drv);
}

static int __init init(void)
{
	struct jitimer_driver *drv = &jitimer_driver;
	struct file_operations *fops = drv->fops;
	struct proc_dir_entry *proc;
	char name[15]; /* strlen("driver/")+strlen(drv->name)+1 */
	int err;

	err = snprintf(name, sizeof(name), "driver/%s", drv->name);
	if (err < 0)
		return err;
	fops->owner	= THIS_MODULE;
	fops->read	= seq_read;
	fops->write	= write;
	fops->open	= open;
	fops->release	= single_release;
	proc = proc_create_data(name, S_IRUGO|S_IWUSR, NULL, fops, drv);
	if (IS_ERR(proc))
		return PTR_ERR(proc);
	drv->delay	= HZ*drv->default_delay_ms/MSEC_PER_SEC;
	drv->proc	= proc;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct jitimer_driver *drv = &jitimer_driver;
	proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Timer module");
