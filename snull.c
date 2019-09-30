/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/netdevice.h>

static struct snull_driver {
	struct net_device	*devs[2];
	struct net_device_ops	ops;
} snull_driver;

static void setup(struct net_device *dev)
{
	return;
}

static int init_driver(struct snull_driver *drv)
{
	memset(&drv->ops, 0, sizeof(struct net_device_ops));
	return 0;
}

static int init(void)
{
	struct snull_driver *drv = &snull_driver;
	int i, end = ARRAY_SIZE(drv->devs);
	struct net_device *dev;
	int err;

	err = init_driver(drv);
	if (err)
		return err;

	for (i = 0; i < end; i++) {
		dev = alloc_netdev(0, "sn%d", NET_NAME_ENUM, setup);
		if (!dev) {
			err = -ENOMEM;
			end = i;
			goto err;
		}
		dev->netdev_ops = &drv->ops;
		err = register_netdev(dev);
		if (err) {
			free_netdev(dev);
			end = i;
			goto err;
		}
		drv->devs[i] = dev;
	}
	return 0;
err:
	for (i = 0; i < end; i++)
		free_netdev(drv->devs[i]);
	return err;

}
module_init(init);

static void cleanup(void)
{
	struct snull_driver *drv = &snull_driver;
	struct net_device **dev, **end = drv->devs+ARRAY_SIZE(drv->devs);

	for (dev = drv->devs; dev != end; dev++) {
		unregister_netdev(*dev);
		free_netdev(*dev);
	}
}
module_exit(cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keith Noguchi <keith.noguchi@gmail.com>");
MODULE_DESCRIPTION("Simple Network Utility presented in LDD");
