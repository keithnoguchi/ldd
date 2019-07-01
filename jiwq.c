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
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

struct jiwq_context {
	unsigned long		call_nr;
	atomic_t		retry_nr;
	unsigned long		prev_jiffies;
	unsigned long		expire;
	struct seq_file		*m;
	struct jiwq_driver	*drv;
	struct completion	done;
	struct delayed_work	base;
};

static struct jiwq_driver {
	unsigned int		type;
#define JIWQ_TYPE_DELAY		(1 << 0)
#define JIWQ_TYPE_SINGLE	(1 << 1)
#define JIWQ_TYPE_SHARED	(1 << 2)
	bool			(*queue)(struct jiwq_driver *drv,
					 struct jiwq_context *ctx);
	void			(*cancel)(struct jiwq_context *ctx);
	unsigned long		delay;
	const unsigned int	default_retry_nr;
	const unsigned int	default_delay_ms;
	struct workqueue_struct	*wq;
	struct proc_dir_entry	*proc;
	struct file_operations	fops;
	const char		*const name;
} jiwq_drivers[] = {
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= 0,
		.name			= "jiwq",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= JIWQ_TYPE_DELAY,
		.name			= "jiwqdelay",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= JIWQ_TYPE_SINGLE,
		.name			= "jisinglewq",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= JIWQ_TYPE_SINGLE|JIWQ_TYPE_DELAY,
		.name			= "jisinglewqdelay",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= JIWQ_TYPE_SHARED,
		.name			= "jisharedwq",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.default_delay_ms	= 0,	/* no delay */
		.proc			= NULL,
		.type			= JIWQ_TYPE_SHARED|JIWQ_TYPE_DELAY,
		.name			= "jisharedwqdelay",
	},
};

static bool queue(struct jiwq_driver *drv, struct jiwq_context *ctx)
{
	return queue_work(drv->wq, &ctx->base.work);
}

static bool queue_delayed(struct jiwq_driver *drv, struct jiwq_context *ctx)
{
	return queue_delayed_work(drv->wq, &ctx->base, drv->delay);
}

static void cancel(struct jiwq_context *ctx)
{
	if (!cancel_work_sync(&ctx->base.work))
		flush_work(&ctx->base.work);
}

static void cancel_delayed(struct jiwq_context *ctx)
{
	if (!cancel_delayed_work_sync(&ctx->base))
		flush_delayed_work(&ctx->base);
}

static void work(struct work_struct *w)
{
	struct jiwq_context *ctx = container_of(to_delayed_work(w),
						struct jiwq_context,
						base);
	struct jiwq_driver *drv = ctx->drv;
	unsigned long now = jiffies;

	ctx->call_nr++;
	if (unlikely(ctx->expire))
		if (likely(time_before(now, ctx->expire)))
			goto again;
	seq_printf(ctx->m, "%10ld %6ld %8ld %6ld %9d %9d %3d %-21s\n",
		   now&0xffffffff, (long)(now-ctx->prev_jiffies),
		   ctx->call_nr, in_interrupt(), in_atomic(),
		   task_pid_nr(current), smp_processor_id(),
		   current->comm);
	ctx->call_nr		= 0;
	ctx->prev_jiffies	= now;
	if (unlikely(ctx->expire))
		ctx->expire	= now + drv->delay;
	if (atomic_dec_return(&ctx->retry_nr) <= 0) {
		complete(&ctx->done);
		return;
	}
again:
	(*drv->queue)(drv, ctx);
}

static int show(struct seq_file *m, void *v)
{
	struct jiwq_driver *drv = m->private;
	unsigned long now = jiffies;
	struct jiwq_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(struct jiwq_context), GFP_KERNEL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	init_completion(&ctx->done);
	if (drv->type & JIWQ_TYPE_DELAY)
		INIT_DELAYED_WORK(&ctx->base, work);
	else
		INIT_WORK(&ctx->base.work, work);
	atomic_set(&ctx->retry_nr, drv->default_retry_nr);
	ctx->m			= m;
	ctx->drv		= drv;
	ctx->prev_jiffies	= now;
	if (unlikely(drv->delay))
		ctx->expire	= now+drv->delay;
	seq_printf(m, "%10s %6s %8s %6s %9s %9s %3s %-21s\n",
		   "time", "delta", "call", "inirq", "inatomic",
		   "pid", "cpu", "cmd");
	seq_printf(m, "%10ld %6d %8d %6ld %9d %9d %3d %-21s\n",
		   now&0xffffffff, 0, 0, in_interrupt(),
		   in_atomic(), task_pid_nr(current),
		   smp_processor_id(), current->comm);
	if (!(*drv->queue)(drv, ctx)) {
		ret = -EINVAL;
		goto done;
	}
	if (wait_for_completion_interruptible(&ctx->done)) {
		ret = -ERESTARTSYS;
		goto done;
	}
	ret = 0;
done:
	(*drv->cancel)(ctx);
	kfree(ctx);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct jiwq_driver *drv = PDE_DATA(file_inode(fp));
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
	struct jiwq_driver *drv = PDE_DATA(ip);
	return single_open(fp, show, drv);
}

static int __init init(void)
{
	struct jiwq_driver *drv = jiwq_drivers;
	struct jiwq_driver *end = drv+ARRAY_SIZE(jiwq_drivers);
	char path[23]; /* strlen("driver/")+strlen(drv[3].name)+1 */
	int err;

	for (drv = jiwq_drivers; drv != end; drv++) {
		struct file_operations *fops;
		struct workqueue_struct *wq;
		struct proc_dir_entry *proc;
		err = snprintf(path, sizeof(path), "driver/%s", drv->name);
		if (err < 0) {
			end = drv;
			goto err;
		}
		if (drv->type & JIWQ_TYPE_SHARED)
			wq = system_wq;
		else if (drv->type & JIWQ_TYPE_SINGLE)
			wq = create_singlethread_workqueue(drv->name);
		else
			wq = create_workqueue(drv->name);
		if (IS_ERR(wq)) {
			err = PTR_ERR(wq);
			end = drv;
			goto err;
		}
		drv->queue	= queue;
		drv->cancel	= cancel;
		if (drv->type & JIWQ_TYPE_DELAY) {
			drv->queue	= queue_delayed;
			drv->cancel	= cancel_delayed;
		}
		drv->delay	= HZ*drv->default_delay_ms/MSEC_PER_SEC;
		drv->wq		= wq;
		fops		= &drv->fops;
		fops->owner	= THIS_MODULE;
		fops->read	= seq_read;
		fops->write	= write;
		fops->open	= open;
		fops->release	= seq_release;
		proc = proc_create_data(path, S_IWUSR|S_IRUGO, NULL, fops, drv);
		if (IS_ERR(proc)) {
			if (!(drv->type & JIWQ_TYPE_SHARED))
				destroy_workqueue(drv->wq);
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc	= proc;
	}
	return 0;
err:
	for (drv = jiwq_drivers; drv != end; drv++) {
		if (!(drv->type & JIWQ_TYPE_SHARED))
			destroy_workqueue(drv->wq);
		proc_remove(drv->proc);
	}
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jiwq_driver *drv = jiwq_drivers;
	struct jiwq_driver *end = drv+ARRAY_SIZE(jiwq_drivers);

	for (drv = jiwq_drivers; drv != end; drv++) {
		flush_workqueue(drv->wq);
		if (!(drv->type & JIWQ_TYPE_SHARED))
			destroy_workqueue(drv->wq);
		proc_remove(drv->proc);
	}
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Work Queue based delay module");
