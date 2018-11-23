/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DRIVER_LDD_H
#define _DRIVER_LDD_H

extern struct bus_type ldd_bus_type;

static inline int ldd_register_device(struct device *dev)
{
	dev->bus = &ldd_bus_type;
	return device_register(dev);
}

static inline void ldd_unregister_device(struct device *dev)
{
	device_unregister(dev);
}

static inline void ldd_release_device(struct device *dev)
{
	printk(KERN_INFO "release %s device\n", dev_name(dev));
}

static inline int ldd_register_driver(struct device_driver *drv)
{
	drv->bus = &ldd_bus_type;
	return driver_register(drv);
}

static inline void ldd_unregister_driver(struct device_driver *drv)
{
	driver_unregister(drv);
}

#endif /* _DRIVER_LDD_H */
