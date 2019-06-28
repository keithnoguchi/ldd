/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/completion.h>
#include <linux/interrupt.h>

struct jitasklet_context {
	atomic_t		retry_nr;
	unsigned long		prev_jiffies;
	unsigned long		expire;
	struct seq_file		*m;
	struct jitasklet_driver	*drv;
	struct completion	done;
	struct tasklet_struct	base;
};

static struct jitasklet_driver {
	unsigned long		delay;		/* in jiffies */
	const unsigned int	default_retry_nr;
	const unsigned int	default_delay_ms;
	struct proc_dir_entry	*proc;
	struct file_operations	fops;
	void			(*schedule)(struct tasklet_struct *t);
	const char		*const name;
} jitasklet_drivers[] = {
	{
		.default_retry_nr	= 5,	/* 5 retry */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.schedule		= tasklet_schedule,
		.name			= "jitasklet",
	},
	{
		.default_retry_nr	= 5,	/* 5 retry */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.schedule		= tasklet_hi_schedule,
		.name			= "jitasklethi",
	},
};

static void tasklet(unsigned long arg)
{
	struct jitasklet_context *ctx = (struct jitasklet_context *)arg;
	struct jitasklet_driver *drv = ctx->drv;
	unsigned long now = jiffies;

	if (unlikely(ctx->expire))
		if (time_before(now, ctx->expire))
			goto again;
	seq_printf(ctx->m, "%10ld %6ld %6ld %9d %9d %3d %-30s\n",
		   now&0xffffffff, (long)(now-ctx->prev_jiffies),
		   in_interrupt(), in_atomic(), task_pid_nr(current),
		   smp_processor_id(), current->comm);
	if (atomic_dec_return(&ctx->retry_nr) < 0) {
		complete(&ctx->done);
		return;
	}
	ctx->prev_jiffies	= now;
	if (unlikely(ctx->expire))
		ctx->expire	= now + drv->delay;
again:
	(*drv->schedule)(&ctx->base);
}

static int show(struct seq_file *m, void *v)
{
	struct jitasklet_driver *drv = m->private;
	unsigned long now = jiffies;
	struct jitasklet_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(struct jitasklet_context), GFP_KERNEL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	init_completion(&ctx->done);
	tasklet_init(&ctx->base, tasklet, (unsigned long)ctx);
	atomic_set(&ctx->retry_nr, drv->default_retry_nr);
	ctx->m			= m;
	ctx->drv		= drv;
	ctx->prev_jiffies	= now;
	if (unlikely(drv->delay))
		ctx->expire	= now + drv->delay;
	seq_printf(ctx->m, "%10s %6s %6s %9s %9s %3s %-30s\n",
		   "time", "delta", "inirq", "inatomic", "pid", "cpu", "cmd");
	seq_printf(ctx->m, "%10ld %6d %6ld %9d %9d %3d %-30s\n",
		   now&0xffffffff, 0, in_interrupt(), in_atomic(),
		   task_pid_nr(current), smp_processor_id(), current->comm);
	(*drv->schedule)(&ctx->base);
	if (wait_for_completion_interruptible(&ctx->done)) {
		ret = -ERESTARTSYS;
		goto done;
	}
	ret = 0;
done:
	atomic_set(&ctx->retry_nr, 0);
	tasklet_kill(&ctx->base);
	kfree(ctx);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct jitasklet_driver *drv = PDE_DATA(file_inode(fp));
	char val[80];
	long ms;
	int err;

	if (copy_from_user(val, buf, sizeof(val)))
		return -EFAULT;
	err = kstrtol(val, 10, &ms);
	if (err)
		return err;
	if (ms < 0 || ms > MSEC_PER_SEC)
		return -EINVAL;
	drv->delay = HZ*ms/MSEC_PER_SEC;
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct jitasklet_driver *drv = PDE_DATA(ip);
	return single_open(fp, show, drv);
}

static int __init init(void)
{
	struct jitasklet_driver *drv = jitasklet_drivers;
	struct jitasklet_driver *end = drv+ARRAY_SIZE(jitasklet_drivers);
	char name[20]; /* strlen("driver/")+strlen(drv[1]->name)+1 */
	int err;

	for (drv = jitasklet_drivers; drv != end; drv++) {
		struct file_operations *fops;
		struct proc_dir_entry *proc;
		err = snprintf(name, sizeof(name), "driver/%s", drv->name);
		if (err < 0) {
			end = drv;
			goto err;
		}
		fops		= &drv->fops;
		fops->owner	= THIS_MODULE;
		fops->read	= seq_read;
		fops->write	= write;
		fops->open	= open;
		fops->release	= seq_release;
		proc = proc_create_data(name, S_IWUSR|S_IRUGO, NULL, fops, drv);
		if (IS_ERR(proc)) {
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc	= proc;
		drv->delay	= HZ*drv->default_delay_ms/MSEC_PER_SEC;
	}
	return 0;
err:
	for (drv = jitasklet_drivers; drv != end; drv++)
		proc_remove(drv->proc);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jitasklet_driver *drv = jitasklet_drivers;
	struct jitasklet_driver *end = drv+ARRAY_SIZE(jitasklet_drivers);

	for ( ; drv != end; drv++)
		proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just in Time tasklet based delay module");
