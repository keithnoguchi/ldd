/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/param.h>

static struct hz_driver {
	struct proc_dir_entry	*proc;
	const char		*const name;
} hz_driver = {
	.name	= "hz",
};

static int show(struct seq_file *m, void *v)
{
	seq_printf(m, "%7s %-7s\n", "HZ", "USER_HZ");
	seq_printf(m, "%7d %-7d\n", HZ, USER_HZ);
	return 0;
}

static int __init init(void)
{
	struct hz_driver *drv = &hz_driver;
	struct proc_dir_entry *proc;
	char path[10]; /* strlen("driver/")+strlen(drv->name) */
	int err;

	err = snprintf(path, sizeof(path), "driver/%s", drv->name);
	if (err < 0)
		return err;
	proc = proc_create_single(path, 0, NULL, show);
	if (IS_ERR(proc))
		return PTR_ERR(proc);
	drv->proc = proc;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct hz_driver *drv = &hz_driver;
	proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("HZ module to show the HZ and USER_HZ variables");
