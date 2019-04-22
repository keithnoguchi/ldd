/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/err.h>

static struct proc_driver {
	const char		*const version;
	struct proc_dir_entry	*top;
	struct device_driver	base;
} proc_driver = {
	.version	= "1.0.0",
	.base.name	= "proc",
	.base.owner	= THIS_MODULE,
};

static int show_version(struct seq_file *seq, void *offset)
{
	struct proc_driver *drv = seq->private;
	seq_printf(seq, "%s\n", drv->version);
	return 0;
}

static int __init init(void)
{
	struct proc_driver *drv = &proc_driver;
	struct proc_dir_entry *dir;
	char path[16]; /* driver/... */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return -EINVAL;
	dir = proc_mkdir_data(path, S_IRUGO|S_IXUGO, NULL, drv);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	drv->top = dir;
	dir = proc_create_single_data("version", S_IRUGO, drv->top, show_version, drv);
	if (IS_ERR(dir))
		goto err;
	return 0;
err:
	proc_remove(drv->top);
	return PTR_ERR(dir);
}
module_init(init);

static void __exit term(void)
{
	struct proc_driver *drv = &proc_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("/proc sample driver");
