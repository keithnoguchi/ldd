/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/sched.h>

struct jitimer_context {
	unsigned int		retry_nr;
	unsigned long		prev_jiffies;
	struct seq_file		*m;
	struct jitimer_driver	*drv;
	wait_queue_head_t	wq;
	struct timer_list	t;
};

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

static void timer(struct timer_list *t)
{
	struct jitimer_context *ctx = container_of(t, struct jitimer_context, t);
	struct jitimer_driver *drv = ctx->drv;
	unsigned long now = jiffies;

	seq_printf(ctx->m, "%10ld %6ld %6ld %9d %9d %3d %-30s\n",
		   now&0xffffffff, (long)(now - ctx->prev_jiffies),
		   in_interrupt(), in_atomic(),
		   task_pid_nr(current), smp_processor_id(), current->comm);
	if (!--ctx->retry_nr) {
		wake_up_interruptible(&ctx->wq);
		return;
	}
	ctx->prev_jiffies = now;
	mod_timer(&ctx->t, now + drv->delay);
}

static int show(struct seq_file *m, void *v)
{
	struct jitimer_driver *drv = m->private;
	struct jitimer_context *ctx;
	unsigned long now = jiffies;
	int ret;

	ctx = kzalloc(sizeof(struct jitimer_context), GFP_KERNEL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	init_waitqueue_head(&ctx->wq);
	timer_setup(&ctx->t, timer, 0);
	ctx->prev_jiffies	= now;
	ctx->t.expires		= now + drv->delay;
	ctx->retry_nr		= drv->retry_nr;
	ctx->drv		= drv;
	ctx->m			= m;
	seq_printf(m, "%10s %6s %6s %9s %9s %3s %-30s\n",
		   "time", "delta", "inirq", "inatomic", "pid", "cpu", "cmd");
	seq_printf(m, "%10ld %6d %6ld %9d %9d %3d %-30s\n",
		   now&0xffffffff, 0, in_interrupt(), in_atomic(),
		   task_pid_nr(current), smp_processor_id(), current->comm);
	add_timer(&ctx->t);
	if (wait_event_interruptible(ctx->wq, !ctx->retry_nr)) {
		ret = -ERESTARTSYS;
		goto out;
	}
	ret = 0;
out:
	del_timer_sync(&ctx->t);
	kfree(ctx);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct jitimer_driver *drv = PDE_DATA(file_inode(fp));
	char val[80];
	long ms;
	int ret;

	if (copy_from_user(val, buf, sizeof(val)))
		return -EFAULT;
	ret = kstrtol(val, 10, &ms);
	if (ret)
		return ret;
	if (ms < 0 || ms > MSEC_PER_SEC)
		return -EINVAL;
	if (!ms)
		ms = drv->default_delay_ms;
	drv->delay = HZ*ms/MSEC_PER_SEC;
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
