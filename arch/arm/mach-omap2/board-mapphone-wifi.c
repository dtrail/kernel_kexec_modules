/* linux/arch/arm/mach-omap2/board-mapphone-wifi.c
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/err.h>
#include <linux/gpio_mapping.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/wifi_tiwlan.h>
#include <plat/board-mapphone.h>

#include <linux/debugfs.h>

static unsigned long mapphone_wifi_pmena_gpio;
static unsigned long mapphone_wifi_irq_gpio;

static int mapphone_wifi_cd = 0;		/* WIFI virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

int omap_wifi_status_register(void (*callback)(int card_present,
						void *dev_id), void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

int omap_wifi_status(int irq)
{
	return mapphone_wifi_cd;
}

int mapphone_wifi_set_carddetect(int val)
{
	printk("%s: %d\n", __func__, val);
	mapphone_wifi_cd = val;
	if (wifi_status_cb) {
		wifi_status_cb(val, wifi_status_cb_devid);
	} else
		printk(KERN_WARNING "%s: Nobody to notify\n", __func__);
	return 0;
}
#ifndef CONFIG_WIFI_CONTROL_FUNC
EXPORT_SYMBOL(mapphone_wifi_set_carddetect);
#endif

static int mapphone_wifi_power_state;

int mapphone_wifi_power(int on)
{
	printk("%s: %d\n", __func__, on);
	/*
	 * Change VIO mode when wifi power on/off
	 * it's a risk that this function may be called in an ISR context
	 * optimize is needed in wifi driver
	 */
	/* change_vio_mode(1, on); */
	gpio_set_value(mapphone_wifi_pmena_gpio, on);
	mapphone_wifi_power_state = on;
	return 0;
}
#ifndef CONFIG_WIFI_CONTROL_FUNC
EXPORT_SYMBOL(mapphone_wifi_power);
#endif

static int mapphone_wifi_reset_state;
int mapphone_wifi_reset(int on)
{
	printk("%s: %d\n", __func__, on);
	mapphone_wifi_reset_state = on;
	return 0;
}
#ifndef CONFIG_WIFI_CONTROL_FUNC
EXPORT_SYMBOL(mapphone_wifi_reset);
#endif

struct wifi_platform_data mapphone_wifi_control = {
        .set_power	= mapphone_wifi_power,
	.set_reset	= mapphone_wifi_reset,
	.set_carddetect	= mapphone_wifi_set_carddetect,
};

#ifdef CONFIG_WIFI_CONTROL_FUNC

static struct resource mapphone_wifi_resources[1];
static struct platform_device mapphone_wifi_device;

static void wifi_res_init(void)
{
	mapphone_wifi_resources[0].name	= "device_wifi_irq";
	mapphone_wifi_resources[0].start =
		OMAP_GPIO_IRQ(mapphone_wifi_irq_gpio);
	mapphone_wifi_resources[0].end = OMAP_GPIO_IRQ(mapphone_wifi_irq_gpio);
	mapphone_wifi_resources[0].flags =
		IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE;

	mapphone_wifi_device.name = "device_wifi";
	mapphone_wifi_device.id	= 1;
	mapphone_wifi_device.num_resources =
		ARRAY_SIZE(mapphone_wifi_resources);
	mapphone_wifi_device.resource = mapphone_wifi_resources;
	mapphone_wifi_device.dev.platform_data = &mapphone_wifi_control;
}

#endif

static int __init mapphone_wifi_init(void)
{
	int ret;

	printk("%s: start\n", __func__);

	mapphone_wifi_pmena_gpio = get_gpio_by_name("wlan_pmena");
	mapphone_wifi_irq_gpio = get_gpio_by_name("wlan_irqena");
	wifi_res_init();

	ret = gpio_request(mapphone_wifi_irq_gpio, "wifi_irq");
	if (ret < 0) {
		printk(KERN_ERR "%s: can't reserve GPIO: %d\n", __func__,
			mapphone_wifi_irq_gpio);
		goto out;
	}
	ret = gpio_request(mapphone_wifi_pmena_gpio, "wifi_pmena");
	if (ret < 0) {
		printk(KERN_ERR "%s: can't reserve GPIO: %d\n", __func__,
			mapphone_wifi_pmena_gpio);
		gpio_free(mapphone_wifi_irq_gpio);
		goto out;
	}
	gpio_direction_input(mapphone_wifi_irq_gpio);
	gpio_direction_output(mapphone_wifi_pmena_gpio, 0);
#ifdef CONFIG_WIFI_CONTROL_FUNC
	ret = platform_device_register(&mapphone_wifi_device);
#endif
out:
        return ret;
}

device_initcall(mapphone_wifi_init);

#if defined(CONFIG_DEBUG_FS)

static int mapphonemmc_dbg_wifi_reset_set(void *data, u64 val)
{
	mapphone_wifi_reset((int) val);
	return 0;
}

static int mapphonemmc_dbg_wifi_reset_get(void *data, u64 *val)
{
	*val = mapphone_wifi_reset_state;
	return 0;
}

static int mapphonemmc_dbg_wifi_cd_set(void *data, u64 val)
{
	mapphone_wifi_set_carddetect((int) val);
	return 0;
}

static int mapphonemmc_dbg_wifi_cd_get(void *data, u64 *val)
{
	*val = mapphone_wifi_cd;
	return 0;
}

static int mapphonemmc_dbg_wifi_pwr_set(void *data, u64 val)
{
	mapphone_wifi_power((int) val);
	return 0;
}

static int mapphonemmc_dbg_wifi_pwr_get(void *data, u64 *val)
{
	*val = mapphone_wifi_power_state;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mapphonemmc_dbg_wifi_reset_fops,
			mapphonemmc_dbg_wifi_reset_get,
			mapphonemmc_dbg_wifi_reset_set, "%llu\n");

DEFINE_SIMPLE_ATTRIBUTE(mapphonemmc_dbg_wifi_cd_fops,
			mapphonemmc_dbg_wifi_cd_get,
			mapphonemmc_dbg_wifi_cd_set, "%llu\n");

DEFINE_SIMPLE_ATTRIBUTE(mapphonemmc_dbg_wifi_pwr_fops,
			mapphonemmc_dbg_wifi_pwr_get,
			mapphonemmc_dbg_wifi_pwr_set, "%llu\n");

static int __init mapphonemmc_dbg_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("mapphone_mmc_dbg", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("wifi_reset", 0644, dent, NULL,
			    &mapphonemmc_dbg_wifi_reset_fops);
	debugfs_create_file("wifi_cd", 0644, dent, NULL,
			    &mapphonemmc_dbg_wifi_cd_fops);
	debugfs_create_file("wifi_pwr", 0644, dent, NULL,
			    &mapphonemmc_dbg_wifi_pwr_fops);
	return 0;
}

device_initcall(mapphonemmc_dbg_init);
#endif
