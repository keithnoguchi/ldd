/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DRIVER_LDD_H
#define _DRIVER_LDD_H

/* ldd_bus_type is the top level virtual bus which hosts
 * all the ldd devices. */
struct bus_type ldd_bus_type = {
	.name		= "ldd",
};

#endif /* _DRIVER_LDD_H */
