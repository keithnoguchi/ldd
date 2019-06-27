/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct jitasklet_driver {
	struct proc_dir_entry	*proc;
	struct file_operations	fops;
	const char		*const name;
} jitasklet_drivers[] = {
	{
		.proc	= NULL,
		.name	= "jitasklet",
	},
	{
		.proc	= NULL,
		.name	= "jitasklethi",
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
	struct jitasklet_driver *drv = PDE_DATA(ip);
	return single_open(fp, show, drv);
}

static int showhi(struct seq_file *m, void *v)
{
	return 0;
}

static int openhi(struct inode *ip, struct file *fp)
{
	struct jitasklet_driver *drv = PDE_DATA(ip);
	return single_open(fp, showhi, drv);
}

static int __init init(void)
{
	struct jitasklet_driver *drv = jitasklet_drivers;
	struct jitasklet_driver *end = drv+ARRAY_SIZE(jitasklet_drivers);
	struct file_operations *fops;
	char name[20]; /* strlen("driver/")+strlen(drv[1]->name)+1 */
	int err;

	/* standard tasklet file operations */
	fops		= &drv[0].fops;
	fops->owner	= THIS_MODULE;
	fops->read	= seq_read;
	fops->write	= write;
	fops->open	= open;
	fops->release	= seq_release;
	/* high priority tasklet file operations */
	fops		= &drv[1].fops;
	fops->owner	= THIS_MODULE;
	fops->read	= seq_read;
	fops->write	= write;
	fops->open	= openhi;
	fops->release	= seq_release;
	for (drv = jitasklet_drivers; drv != end; drv++) {
		struct proc_dir_entry *proc;

		err = snprintf(name, sizeof(name), "driver/%s", drv->name);
		if (err < 0) {
			end = drv;
			goto err;
		}
		proc = proc_create_data(name, S_IWUSR|S_IRUGO, NULL, fops, drv);
		if (IS_ERR(proc)) {
			err = PTR_ERR(proc);
			end = drv;
			goto err;
		}
		drv->proc = proc;
	}
	return 0;
err:
	for (drv = jitasklet_drivers; drv != end; drv++)
		proc_remove(drv->proc);
	return err;
}
module_init(init);

static void __exit term(void)
{
	struct jitasklet_driver *drv = jitasklet_drivers;
	struct jitasklet_driver *end = drv+ARRAY_SIZE(jitasklet_drivers);

	for ( ; drv != end; drv++)
		proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just in Time tasklet based delay module");
