/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct seq_driver {
	struct proc_dir_entry	*top;
	struct device_driver	base;
} seq_driver = {
	.top		= NULL,
	.base.name	= "seq",
	.base.owner	= THIS_MODULE,
};

static int __init init(void)
{
	struct seq_driver *drv = &seq_driver;
	struct proc_dir_entry *dir;
	char path[11];
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return -1;
	dir = proc_mkdir(path, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct seq_driver *drv = &seq_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("<linux/seq_file.h> example");
