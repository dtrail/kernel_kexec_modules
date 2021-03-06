/*
 * arch/arm/mach-omap2/board-mapphone-uart.c
 *
 * Copyright (C) 2011 Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_mapping.h>
#include <linux/serial_reg.h>
#include <linux/wakelock.h>
#include <plat/omap-serial.h>

#include <mach/dt_path.h>
#include <asm/prom.h>

#include "mux.h"

static int wake_gpio_strobe = -1;
static struct wake_lock uart_lock;

static void mapphone_uart_hold_wakelock(void *up, int flag);
static void mapphone_uart_probe(struct uart_omap_port *up);
static void mapphone_uart_remove(struct uart_omap_port *up);
static void mapphone_uart_wake_peer(struct uart_omap_port *up);

/* Give 1s wakelock time for each port */
static u8 wakelock_length[OMAP_MAX_HSUART_PORTS] = {2, 2, 2, 2};

#define MAX_UART_MUX_ALTERNATIVES 2

static struct uart_mux_alternatives {
	u16	padconf;
	u16	padconf_wake_ev;
	u32	wk_mask;
} mapphone_uart_mux_alternatives[OMAP_MAX_HSUART_PORTS][MAX_UART_MUX_ALTERNATIVES] __initdata = {
	[UART1] = {
		/* no MAIN configuration for uart1 */
		[1] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_MCSPI1_CS2_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_3,
			.wk_mask =
				OMAP4_MCSPI1_CS1_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_MCSPI1_CS2_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_MCSPI1_CS3_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART3_CTS_RCTX_DUPLICATEWAKEUPEVENT_MASK,
		},
	},
	[UART2] = {
		[0] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_UART2_RX_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_3,
			.wk_mask =
				OMAP4_UART2_TX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART2_RX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART2_RTS_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART2_CTS_DUPLICATEWAKEUPEVENT_MASK,
		},
		[1] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_USBA0_OTG_DP_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_3,
			.wk_mask =
				OMAP4_UART2_TX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART2_RX_DUPLICATEWAKEUPEVENT_MASK,
		},
	},
	[UART3] = {
		[0] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_UART3_RX_IRRX_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_4,
			.wk_mask =
				OMAP4_UART3_TX_IRTX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART3_RX_IRRX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART3_RTS_SD_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART3_CTS_RCTX_DUPLICATEWAKEUPEVENT_MASK,
		},
		[1] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_UART3_RX_IRRX_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_4,
			.wk_mask =
				OMAP4_UART3_TX_IRTX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART3_RX_IRRX_DUPLICATEWAKEUPEVENT_MASK,
		},
	},
	[UART4] = {
		[0] = {
			.padconf = OMAP4_CTRL_MODULE_PAD_UART4_RX_OFFSET,
			.padconf_wake_ev = OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_4,
			.wk_mask =
				OMAP4_UART4_TX_DUPLICATEWAKEUPEVENT_MASK |
				OMAP4_UART4_RX_DUPLICATEWAKEUPEVENT_MASK,
		},
	},
};

static struct omap_uart_port_info omap_serial_platform_data[] = {
	{
		.use_dma		= 0,
		.dma_rx_buf_size	= DEFAULT_RXDMA_BUFSIZE,
		.dma_rx_poll_rate	= DEFAULT_RXDMA_POLLRATE,
		.dma_rx_timeout		= DEFAULT_RXDMA_TIMEOUT,
		.idle_timeout		= 1000, /* Reduce idle time, 5s -> 1s */
		.flags			= 1,
		.plat_hold_wakelock	= mapphone_uart_hold_wakelock,
		.padconf		=
			OMAP4_CTRL_MODULE_PAD_MCSPI1_CS2_OFFSET,
		.padconf_wake_ev	=
			OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_3,
		.wk_mask		=
			OMAP4_MCSPI1_CS1_DUPLICATEWAKEUPEVENT_MASK |
			OMAP4_MCSPI1_CS2_DUPLICATEWAKEUPEVENT_MASK |
			OMAP4_MCSPI1_CS3_DUPLICATEWAKEUPEVENT_MASK |
			OMAP4_UART3_CTS_RCTX_DUPLICATEWAKEUPEVENT_MASK,
		.board_uart_probe	= mapphone_uart_probe,
		.board_uart_remove	= mapphone_uart_remove,
		.board_wake_peer	= mapphone_uart_wake_peer,
		.rts_padconf		=
			OMAP4_CTRL_MODULE_PAD_MCSPI1_CS3_OFFSET,
		.ctsrts			= UART_EFR_CTS | UART_EFR_RTS,
		.console_port	= 0,
		.webtop_port	= 0,
	},
	{
		.use_dma		= 0,
		.dma_rx_buf_size	= DEFAULT_RXDMA_BUFSIZE,
		.dma_rx_poll_rate	= DEFAULT_RXDMA_POLLRATE,
		.dma_rx_timeout		= DEFAULT_RXDMA_TIMEOUT,
		.idle_timeout		= 1000, /* Reduce idle time, 5s -> 1s */
		.flags			= 1,
		.plat_hold_wakelock	= mapphone_uart_hold_wakelock,
		.rts_padconf		=
			OMAP4_CTRL_MODULE_PAD_UART2_RTS_OFFSET,
		.ctsrts			= UART_EFR_CTS | UART_EFR_RTS,
		.console_port	= 0,
		.webtop_port	= 1,
	},
	{
		.use_dma		= 0,
		.dma_rx_buf_size	= DEFAULT_RXDMA_BUFSIZE,
		.dma_rx_poll_rate	= DEFAULT_RXDMA_POLLRATE,
		.dma_rx_timeout		= DEFAULT_RXDMA_TIMEOUT,
		.idle_timeout		= 1000, /* Reduce idle time, 5s -> 1s */
		.flags			= 1,
		.plat_hold_wakelock	= mapphone_uart_hold_wakelock,
		.padconf		=
			OMAP4_CTRL_MODULE_PAD_UART3_RX_IRRX_OFFSET,
		.padconf_wake_ev	=
			OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_4,
		.wk_mask		 =
			OMAP4_UART3_TX_IRTX_DUPLICATEWAKEUPEVENT_MASK |
			OMAP4_UART3_RX_IRRX_DUPLICATEWAKEUPEVENT_MASK,
		.ctsrts			= 0,
		.console_port	= 1,
		.webtop_port	= 0,
	},
	{
		.use_dma		= 0,
		.dma_rx_buf_size	= DEFAULT_RXDMA_BUFSIZE,
		.dma_rx_poll_rate	= DEFAULT_RXDMA_POLLRATE,
		.dma_rx_timeout		= DEFAULT_RXDMA_TIMEOUT,
		.idle_timeout		= 1000, /* Reduce idle time, 5s -> 1s */
		.flags			= 1,
		.plat_hold_wakelock	= mapphone_uart_hold_wakelock,
		.padconf		=
			OMAP4_CTRL_MODULE_PAD_UART4_RX_OFFSET,
		.padconf_wake_ev	=
			OMAP4_CTRL_MODULE_PAD_CORE_PADCONF_WAKEUPEVENT_4,
		.wk_mask		=
			OMAP4_UART4_TX_DUPLICATEWAKEUPEVENT_MASK |
			OMAP4_UART4_RX_DUPLICATEWAKEUPEVENT_MASK,
		.rts_padconf		=
			OMAP4_CTRL_MODULE_PAD_ABE_DMIC_DIN1_OFFSET,
		.cts_padconf		=
			OMAP4_CTRL_MODULE_PAD_ABE_DMIC_CLK1_OFFSET,
		.ctsrts			= 0,
		.console_port	= 0,
		.webtop_port	= 0,
	},
	{
		.flags		= 0
	}
};

#ifdef CONFIG_ARM_OF
static void __init mapphone_serial_dt_pins(int id, struct device_node *node)
{
	u8 pins;
	const void *prop = of_get_property(node, "pins", NULL);

	if (!prop)
		return;

	pins = *((u8 *)prop);

	if ((pins > 0) && (pins < MAX_UART_MUX_ALTERNATIVES)) {
		omap_serial_platform_data[id].padconf =
			mapphone_uart_mux_alternatives[id][pins].padconf;
		omap_serial_platform_data[id].padconf_wake_ev =
			mapphone_uart_mux_alternatives[id][pins].padconf_wake_ev;
		omap_serial_platform_data[id].wk_mask =
			mapphone_uart_mux_alternatives[id][pins].wk_mask;
	}

	return;
}

static void __init mapphone_serial_dt_wakelock(int id, struct device_node *node)
{
	u8 length;
	const void *prop = of_get_property(node, "wakelock", NULL);

	if (!prop)
		return;

	length = *((u8 *)prop);

	if (!length)
		omap_serial_platform_data[id].plat_hold_wakelock = NULL;

	wakelock_length[id] = length;

	return;
}

static void __init mapphone_serial_dt_ctsrts(int id, struct device_node *node)
{
	const void *prop = of_get_property(node, "ctsrts", NULL);

	if (!prop)
		return;

	omap_serial_platform_data[id].ctsrts = *((u8 *)prop);

	return;
}
#endif

void __init mapphone_serial_init(void)
{
#ifdef CONFIG_ARM_OF
	struct device_node *node;
	int i;

	for (i = 0; i < OMAP_MAX_HSUART_PORTS; i++) {
		char path[25];

		snprintf(path, 24, "/System@0/UART@%d", i);
		node = of_find_node_by_path(path);

		if (!node)
			continue;

		mapphone_serial_dt_pins(i, node);
		mapphone_serial_dt_wakelock(i, node);
		mapphone_serial_dt_ctsrts(i, node);

		of_node_put(node);
	}
#endif

	wake_lock_init(&uart_lock, WAKE_LOCK_SUSPEND, "uart_wake_lock");
	omap_serial_init(omap_serial_platform_data);
}

static void mapphone_uart_hold_wakelock(void *up, int flag)
{
	struct uart_omap_port *up2 = (struct uart_omap_port *)up;

	/* Supply 500ms precision on wakelock */
	if (wakelock_length[up2->pdev->id])
		wake_lock_timeout(&uart_lock,
				wakelock_length[up2->pdev->id]*HZ/2);

	return;
}

static void mapphone_uart_probe(struct uart_omap_port *up)
{
	wake_gpio_strobe = get_gpio_by_name("ipc_bpwake_trigger");

	if (wake_gpio_strobe >= 0) {
		if (gpio_request(wake_gpio_strobe,
				 "UART wakeup strobe")) {
			printk(KERN_ERR "Error requesting GPIO\n");
		} else {
			gpio_direction_output(wake_gpio_strobe, 0);
		}
	}
}

static void mapphone_uart_remove(struct uart_omap_port *up)
{
	if (wake_gpio_strobe >= 0)
		gpio_free(wake_gpio_strobe);
}

static void mapphone_uart_wake_peer(struct uart_omap_port *up)
{
	if (wake_gpio_strobe >= 0) {
		gpio_direction_output(wake_gpio_strobe, 1);
		udelay(5);
		gpio_direction_output(wake_gpio_strobe, 0);
		udelay(5);
	}
}
