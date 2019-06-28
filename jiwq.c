/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

struct jiwq_context {
	atomic_t		retry_nr;
	unsigned long		prev_jiffies;
	struct seq_file		*m;
	struct jiwq_driver	*drv;
	struct completion	done;
	struct delayed_work	base;
};

static struct jiwq_driver {
	const unsigned int	default_retry_nr;
	struct workqueue_struct	*wq;
	struct proc_dir_entry	*proc;
	struct file_operations	fops;
	const char		*const name;
} jiwq_drivers[] = {
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.proc			= NULL,
		.name			= "jiwq",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.proc			= NULL,
		.name			= "jiwqdelay",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.proc			= NULL,
		.name			= "jiwqsingle",
	},
	{
		.default_retry_nr	= 5,	/* 5 retries */
		.proc			= NULL,
		.name			= "jiwqsingledelay",
	},
};

static void work(struct work_struct *w)
{
	struct jiwq_context *ctx = container_of(to_delayed_work(w),
						struct jiwq_context,
						base);
	unsigned long now = jiffies;
	seq_printf(ctx->m, "%10ld %6ld %8d %6ld %9d %9d %3d %-21s\n",
		   now&0xffffffff, (long)(now-ctx->prev_jiffies), 0,
		   in_interrupt(), in_atomic(), task_pid_nr(current),
		   smp_processor_id(), current->comm);
	complete(&ctx->done);
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
	INIT_WORK(&ctx->base.work, work);
	atomic_set(&ctx->retry_nr, drv->default_retry_nr);
	ctx->m			= m;
	ctx->drv		= drv;
	ctx->prev_jiffies	= now;
	seq_printf(m, "%10s %6s %8s %6s %9s %9s %3s %-21s\n",
		   "time", "delta", "call", "inirq", "inatomic",
		   "pid", "cpu", "cmd");
	seq_printf(m, "%10ld %6d %8d %6ld %9d %9d %3d %-21s\n",
		   now&0xffffffff, 0, 0, in_interrupt(),
		   in_atomic(), task_pid_nr(current),
		   smp_processor_id(), current->comm);
	if (!queue_work(drv->wq, &ctx->base.work)) {
		ret = -EINVAL;
		goto done;
	}
	if (wait_for_completion_interruptible(&ctx->done)) {
		ret = -ERESTARTSYS;
		goto done;
	}
	ret = 0;
done:
	kfree(ctx);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
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
		if (!strncmp(drv->name, "jiwqsingle", strlen("jiwqsingle")))
			wq = create_singlethread_workqueue(drv->name);
		else
			wq = create_workqueue(drv->name);
		if (IS_ERR(wq)) {
			err = PTR_ERR(wq);
			end = drv;
			goto err;
		}
		drv->wq		= wq;
		fops		= &drv->fops;
		fops->owner	= THIS_MODULE;
		fops->read	= seq_read;
		fops->write	= write;
		fops->open	= open;
		fops->release	= seq_release;
		proc = proc_create_data(path, S_IWUSR|S_IRUGO, NULL, fops, drv);
		if (IS_ERR(proc)) {
			destroy_workqueue(wq);
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc	= proc;
	}
	return 0;
err:
	for (drv = jiwq_drivers; drv != end; drv++) {
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
		destroy_workqueue(drv->wq);
		proc_remove(drv->proc);
	}
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Work Queue based delay module");
