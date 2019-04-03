/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LDD_H
#define _LDD_H

int ldd_register_device(struct device *dev);
void ldd_unregister_device(struct device *dev);
void ldd_release_device(struct device *dev);
int ldd_register_driver(struct device_driver *drv);
void ldd_unregister_driver(struct device_driver *drv);

#endif /* _LDD_H */
