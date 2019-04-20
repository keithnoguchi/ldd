/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/err.h>

static struct proc_driver {
	struct proc_dir_entry	*top;
	struct device_driver	base;
} proc_driver;

static int show(struct seq_file *sf, void *data)
{
	printk(KERN_ALERT "show()\n");
	return 0;
}

static int __init init(void)
{
	struct proc_driver *drv = &proc_driver;
	struct proc_dir_entry *dir;

	dir = proc_create_single_data("driver/proc", 0644, NULL, show, drv);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct proc_driver *drv = &proc_driver;
	if (drv->top)
		proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("/proc file system example");
