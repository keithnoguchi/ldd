/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct jiwq_driver {
	struct proc_dir_entry	*proc;
	struct file_operations	fops;
	const char		*const name;
} jiwq_drivers[] = {
	{
		.proc	= NULL,
		.name	= "jiwq",
	},
	{
		.proc	= NULL,
		.name	= "jiwqdelay",
	},
};

static int show(struct seq_file *m, void *v)
{
	return 0;
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
	char path[17]; /* strlen("driver/")+strlen(drv[1].name)+1 */
	int err;

	for (drv = jiwq_drivers; drv != end; drv++) {
		struct file_operations *fops;
		struct proc_dir_entry *proc;
		err = snprintf(path, sizeof(path), "driver/%s", drv->name);
		if (err < 0) {
			end = drv;
			goto err;
		}
		fops		= &drv->fops;
		fops->owner	= THIS_MODULE;
		fops->read	= seq_read;
		fops->write	= write;
		fops->open	= open;
		fops->release	= seq_release;
		proc = proc_create_data(path, S_IRUSR|S_IWUGO, NULL, fops, drv);
		if (IS_ERR(proc)) {
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc	= proc;
	}
	return 0;
err:
	for (drv = jiwq_drivers; drv != end; drv++)
		proc_remove(drv->proc);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jiwq_driver *drv = jiwq_drivers;
	struct jiwq_driver *end = drv+ARRAY_SIZE(jiwq_drivers);

	for (drv = jiwq_drivers; drv != end; drv++)
		proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Work Queue based delay module");
