/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timekeeping.h>

static struct jit_driver {
	struct proc_dir_entry	*top;
	struct device_driver	base;
	struct seq_operations	sops[1];
} jit_driver = {
	.top		= NULL,
	.base.owner	= THIS_MODULE,
	.base.name	= "jit",
};

static void *start_currenttime(struct seq_file *m, loff_t *pos)
{
	return PDE_DATA(file_inode(m->file));
}

static void stop_currenttime(struct seq_file *m, void *v)
{
	return;
}

static void *next_currenttime(struct seq_file *m, void *v, loff_t *pos)
{
	return m->private;
}

static int show_currenttime(struct seq_file *m, void *v)
{
	struct timespec64 ts;
	u64 ns;

	ktime_get_real_ts64(&ts);
	ns = ktime_get_real_fast_ns();
	seq_printf(m, "0x%08lx 0x%016llx %10lld.%09ld (ktime_get_real_ts64)\n"
		   "%29c %10lld.%09lld (ktime_get_real_fast_ns)\n",
		   jiffies&0xffffffff, get_jiffies_64(),
		   ts.tv_sec, ts.tv_nsec,
		   ' ',
		   ns/NSEC_PER_SEC, ns%NSEC_PER_SEC);
	return 0;
}

static int __init init(void)
{
	struct jit_driver *drv = &jit_driver;
	struct proc_dir_entry *dir;
	char path[11]; /* strlen("driver/")+strlen(drv->base.name) */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return err;
	dir = proc_mkdir(path, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	drv->sops[0].start	= start_currenttime;
	drv->sops[0].stop	= stop_currenttime;
	drv->sops[0].next	= next_currenttime;
	drv->sops[0].show	= show_currenttime;
	dir = proc_create_seq_data("currenttime", 0, drv->top, &drv->sops[0], drv);
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