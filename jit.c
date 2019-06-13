/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

static struct jit_driver {
	struct proc_dir_entry	*top;
	struct device_driver	base;
} jit_driver = {
	.top		= NULL,
	.base.owner	= THIS_MODULE,
	.base.name	= "jit",
};

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
	return 0;
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
