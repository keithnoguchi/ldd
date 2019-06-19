/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/param.h>

static struct hz_driver {
	struct proc_dir_entry	*top;
	const char		*const name;
} hz_driver = {
	.top	= NULL,
	.name	= "hz",
};

static int show_hz(struct seq_file *m, void *v)
{
	seq_printf(m, "%7s %7s\n", "HZ", "USER_HZ");
	seq_printf(m, "%7d %7d\n", HZ, USER_HZ);
	return 0;
}

static int __init init(void)
{
	struct hz_driver *drv = &hz_driver;
	struct proc_dir_entry *top;
	char path[10]; /* strlen("driver/")+strlen(drv->name) */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->name);
	if (err < 0)
		return err;
	top = proc_create_single(path, 0, NULL, show_hz);
	if (IS_ERR(top))
		return PTR_ERR(top);
	drv->top = top;
	return 0;
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
