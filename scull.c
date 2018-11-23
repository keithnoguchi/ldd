/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/device.h>
#include "ldd.h"

/* Scull device. */
static struct device scull_device = {
	.init_name	= "scull0",
	.release	= ldd_release_device,
};

int scull_register(void)
{
	int err;

	printk(KERN_INFO "Let's add scull devices\n");

	err = ldd_register_device(&scull_device);
	if (err)
		return err;

	return 0;
}

void scull_unregister(void)
{
	printk(KERN_INFO "Let's cleanup the scull devices\n");
	ldd_unregister_device(&scull_device);
}
