/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>

static struct jiffies_driver {
	struct proc_dir_entry	*top;
	const unsigned long	max_nr;
	const char		*const name;
	struct seq_operations	sops[1];
} jiffies_driver = {
	.max_nr	= 256,
	.name	= "jiffies",
};

static void *start(struct seq_file *m, loff_t *pos)
{
	struct jiffies_driver *drv = PDE_DATA(file_inode(m->file));
	if (*pos >= drv->max_nr)
		return NULL;
	seq_printf(m, "%-18s %-10s %-18s %s\n%48c %-s\n",
		   "get_cycles()", "jiffies", "jiffies_64",
		   "ktime_get_real_ts64()", ' ', "ktime_get_real_fast_ns()");
	return drv;
}

static void stop(struct seq_file *m, void *v)
{
	return;
}

static void *next(struct seq_file *m, void *v, loff_t *pos)
{
	struct jiffies_driver *drv = v;
	if (++(*pos) >= drv->max_nr)
		return NULL;
	return v;
}

static int show(struct seq_file *m, void *v)
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

static int __init init(void)
{
	struct jiffies_driver *drv = &jiffies_driver;
	struct seq_operations *sops = drv->sops;
	struct proc_dir_entry *top;
	char path[15]; /* strlen("driver/")+strlen(drv->name)+1 */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->name);
	if (err < 0)
		return err;
	sops->start	= start;
	sops->stop	= stop;
	sops->next	= next;
	sops->show	= show;
	top = proc_create_seq_data(path, 0, NULL, sops, drv);
	if (IS_ERR(top))
		return PTR_ERR(top);
	drv->top = top;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct jiffies_driver *drv = &jiffies_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Jiffies module, showing jiffies, jiffies_64 and the like");
