/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/param.h>

static struct hz_driver {
	struct proc_dir_entry	*top;
	struct device_driver	base;
} hz_driver = {
	.top		= NULL,
	.base.owner	= THIS_MODULE,
	.base.name	= "hz",
};

static int show_hz(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", HZ);
	return 0;
}

static int show_user_hz(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", USER_HZ);
	return 0;
}

static int __init init(void)
{
	struct hz_driver *drv = &hz_driver;
	struct proc_dir_entry *top, *d;
	char path[10]; /* strlen("driver/")+strlen(drv->base.name) */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->base.name);
	if (err < 0)
		return err;
	top = proc_mkdir(path, NULL);
	if (IS_ERR(top))
		return PTR_ERR(top);
	d = proc_create_single("hz", 0, top, show_hz);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto err;
	}
	d = proc_create_single("user_hz", 0, top, show_user_hz);
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		goto err;
	}
	drv->top = top;
	return 0;
err:
	proc_remove(top);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct hz_driver *drv = &hz_driver;
	proc_remove(drv->top);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("HZ module to show the HZ and USER_HZ variables");
