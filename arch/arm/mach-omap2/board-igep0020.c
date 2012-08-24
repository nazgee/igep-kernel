/*
 * Copyright (C) 2009-2011 - ISEE 2007 SL
 *
 * Modified from mach-omap2/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/smsc911x.h>
#include <linux/mmc/host.h>

#include <linux/spi/spi.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/i2c/twl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>
#include <plat/usb.h>
#include <plat/display.h>
#include <plat/onenand.h>
#include <plat/pwm.h>

#include "twl-common.h"
#include "mux.h"
#include "hsmmc.h"
#include "board-igep00x0.h"

#define SMSC911X_CS			5
#define SMSC911X_IRQ			176
#define SMSC911X_NRESET			64
#define GPIO_USBH_NRESET		24
#define GPIO_DVI_PUP			170
#define GPIO_LED_D440_GREEN		26
#define GPIO_LED_D440_RED		27
#define GPIO_LED_D240_RED		28

#define IGEP2_RB_GPIO_WIFI_NPD 		94
#define IGEP2_RB_GPIO_WIFI_NRESET	95
#define IGEP2_RB_GPIO_BT_NRESET		137
#define IGEP2_RC_GPIO_WIFI_NPD		138
#define IGEP2_RC_GPIO_WIFI_NRESET	139
#define IGEP2_RC_GPIO_BT_NRESET		137

#define IGEP0020_BOARD_OPT_RS232	(1 << 0)

/* Board configuration options */
struct board_platform_data {
	unsigned options;
};

struct board_platform_data igep0020_board_pdata = {
	.options = 0,
};

/*
 * IGEP2 Hardware Revision Table
 *
 *  --------------------------------------------------------------------------
 * | Id. | Hw Rev.            | HW0 (28) | WIFI_NPD | WIFI_NRESET | BT_NRESET |
 *  --------------------------------------------------------------------------
 * |  0  | B                  |   high   |  gpio94  |   gpio95    |     -     |
 * |  0  | B/C (B-compatible) |   high   |  gpio94  |   gpio95    |  gpio137  |
 * |  1  | C                  |   low    |  gpio138 |   gpio139   |  gpio137  |
 *  --------------------------------------------------------------------------
 */

#define IGEP2_BOARD_HWREV_B	0
#define IGEP2_BOARD_HWREV_C	1

static u8 hwrev;

static void __init igep0020_get_revision(void)
{
	u8 ret;

	omap_mux_init_gpio(GPIO_LED_D240_RED, OMAP_PIN_INPUT);

	if ((gpio_request(GPIO_LED_D240_RED, "GPIO HW0 REV") == 0) &&
	    (gpio_direction_input(GPIO_LED_D240_RED) == 0)) {
		ret = gpio_get_value(GPIO_LED_D240_RED);
		if (ret == 0) {
			pr_info("IGEP: Hardware Rev. C\n");
			hwrev = IGEP2_BOARD_HWREV_C;
		} else if (ret ==  1) {
			pr_info("IGEP: Hardware Rev. B\n");
			hwrev = IGEP2_BOARD_HWREV_B;
		} else {
			pr_err("IGEP: Unknow Hardware Revision\n");
			hwrev = -1;
		}
	} else {
		pr_warning("IGEP: Could not obtain gpio HW0 REV\n");
		pr_err("IGEP: Unknow Hardware Revision\n");
	}

	gpio_free(GPIO_LED_D240_RED);
}

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)

static struct smsc911x_platform_config igep2_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource igep2_smsc911x_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= OMAP_GPIO_IRQ(SMSC911X_IRQ),
		.end	= OMAP_GPIO_IRQ(SMSC911X_IRQ),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(igep2_smsc911x_resources),
	.resource	= igep2_smsc911x_resources,
	.dev		= {
		.platform_data = &igep2_smsc911x_config,
	},
};
#endif

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
#if defined(CONFIG_LIBERTAS_SDIO) || defined(CONFIG_LIBERTAS_SDIO_MODULE)
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
#endif
	{}      /* Terminator */
};

static struct gpio_led igep2_gpio_leds[] = {
	[0] = {
		.name = "d440:red",
		.gpio = GPIO_LED_D440_RED,
		.default_trigger = "default-off"
	},
	[1] = {
		.name = "d440:green",
		.gpio = GPIO_LED_D440_GREEN,
		.default_trigger = "default-off",
	},
	[2] = {
		.name = "d240:red",
		.gpio = GPIO_LED_D240_RED,
		.default_trigger = "default-off",
	},
	[3] = {
		.name = "d240:green",
		.default_trigger = "default-on",
		.gpio = -EINVAL, /* gets replaced */
		.active_low = true,
	},
};

static struct gpio_led_platform_data igep2_led_pdata = {
	.leds           = igep2_gpio_leds,
	.num_leds       = ARRAY_SIZE(igep2_gpio_leds),
};

static struct platform_device igep2_led_device = {
	 .name   = "leds-gpio",
	 .id     = -1,
	 .dev    = {
		 .platform_data  =  &igep2_led_pdata,
	},
};

static void __init igep2_leds_init(void)
{
	platform_device_register(&igep2_led_device);
}

static struct spi_board_info igep2_spidev_board_info __initdata = {
	.modalias	= "spidev",
	.bus_num	= 2,
	.chip_select	= 0,
	.max_speed_hz	= 20000000,
	.mode		= SPI_MODE_2,
};

static int igep2_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap2_hsmmc_init(mmc);

	/*
	 * REVISIT: need ehci-omap hooks for external VBUS
	 * power switch and overcurrent detect
	 */
	gpio_request(gpio + 1, "EHCI NOC");
	gpio_direction_input(gpio + 1);

	/* TWL4030_GPIO_MAX + 0 == ledA, GPIO_USBH_CPEN (out, active low) */
	gpio_request(gpio + TWL4030_GPIO_MAX, "USB_ PEN");
	gpio_direction_output(gpio + TWL4030_GPIO_MAX, 0);

	/* TWL4030_GPIO_MAX + 1 == ledB (out, active low LED) */
	igep2_gpio_leds[3].gpio = gpio + TWL4030_GPIO_MAX + 1;

	return 0;
};

static struct twl4030_gpio_platform_data igep2_twl4030_gpio_pdata = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.setup		= igep2_twl_gpio_setup,
};

#if defined(CONFIG_HAVE_PWM)
static struct omap2_pwm_platform_config pwm1_config = {
	.timer_id = 8,
	.polarity = 1 // Active high
};
static struct platform_device igep2_pwm1_device = {
	.name	= "omap-pwm",
	.id	= 0,
	.dev	= {
		 .platform_data  =  &pwm1_config,
	},
};

static struct omap2_pwm_platform_config pwm2_config = {
	.timer_id = 9,
	.polarity = 1 // Active high
};
static struct platform_device igep2_pwm2_device = {
	.name	= "omap-pwm",
	.id	= 1,
	.dev	= {
		.platform_data  =  &pwm2_config,
	},
};
static struct platform_device *pwm_devices[] __initdata = {
	&igep2_pwm1_device,
	&igep2_pwm2_device
};
static inline void pwm_init(void)
{
	platform_add_devices(pwm_devices, ARRAY_SIZE(pwm_devices));
}
#endif

static int igep0020_enable_dvi(struct omap_dss_device *dssdev)
{
	gpio_direction_output(GPIO_DVI_PUP, 1);

	return 0;
}

static void igep0020_disable_dvi(struct omap_dss_device *dssdev)
{
	gpio_direction_output(GPIO_DVI_PUP, 0);
}

static struct omap_dss_device *dss_devices[] = {
	&igep00x0_dvi_device,
	&igep00x0_tv_device,
	&igep00x0_lcd43_device,
	&igep00x0_lcd70_device,
};

static struct omap_dss_board_info dss_board_data = {
	.num_devices	= ARRAY_SIZE(dss_devices),
	.devices	= dss_devices,
	.default_device	= &igep00x0_dvi_device,
};

static struct platform_device igep0020_dss_device = {
	.name	= "omapdss",
	.id	= -1,
	.dev	= {
		.platform_data = &dss_board_data,
	},
};

static int igep2_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P)
};

static struct matrix_keymap_data igep2_keymap_data = {
	.keymap			= igep2_keymap,
	.keymap_size		= ARRAY_SIZE(igep2_keymap),
};

static struct twl4030_keypad_data igep2_twl4030_keypad_data = {
	.keymap_data	= &igep2_keymap_data,
	.rows		= 4,
	.cols		= 4,
	.rep		= 1,
};

static struct twl4030_platform_data twl4030_pdata = {
	/* platform_data for children goes here */
	.gpio		= &igep2_twl4030_gpio_pdata,
	.keypad		= &igep2_twl4030_keypad_data,
};

static struct i2c_board_info __initdata igep2_i2c3_boardinfo[] = {
	{
		I2C_BOARD_INFO("eeprom", 0x50),
	},
};

static const struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {
	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_UNKNOWN,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset = true,
	.reset_gpio_port[0] = GPIO_USBH_NRESET,
	.reset_gpio_port[1] = -EINVAL,
	.reset_gpio_port[2] = -EINVAL,
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* SMSC9221 LAN Controller */
	OMAP3_MUX(MCSPI1_CS2, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	/* Display Sub System */
	OMAP3_MUX(DSS_PCLK, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_HSYNC, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_VSYNC, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_ACBIAS, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA0, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA1, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA2, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA3, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA4, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA5, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA6, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA7, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA8, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA9, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA10, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA11, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA12, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA13, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA14, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA15, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA16, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA17, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA18, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA19, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA20, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA21, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA22, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA23, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(HDQ_SIO, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	/* McBSP 2 */
	OMAP3_MUX(MCBSP2_FSX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_CLKX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DR, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	/* Serial ports */
	OMAP3_MUX(UART1_TX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(UART1_RX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),
	OMAP3_MUX(UART1_RTS, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(UART1_CTS, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),
	// we do not want to use UART2 with BT
//	OMAP3_MUX(UART2_TX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
//	OMAP3_MUX(UART2_RX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART2_TX, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART2_RX, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART2_RTS, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART2_CTS, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP3_CLKX, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),	// TX
	OMAP3_MUX(MCBSP3_FSX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),		// RX
//	OMAP3_MUX(MCBSP3_DR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),		// RTS
//	OMAP3_MUX(MCBSP3_DX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),		// CTS
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define board_mux	NULL
#endif

#if defined(CONFIG_HAVE_PWM)
static struct omap_board_mux nsc_as_pwm_mux[] = {
	OMAP3_MUX(GPMC_NCS4, OMAP_MUX_MODE3 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(GPMC_NCS7, OMAP_MUX_MODE3 | OMAP_PIN_OUTPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

/* Use UART1 as RS232 (not RS485) */
static struct omap_board_mux uart1_as_rs232_mux[] = {
	OMAP3_MUX(UART1_TX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(UART1_RX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),
	OMAP3_MUX(UART1_RTS, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART1_CTS, OMAP_MUX_MODE7 | OMAP_PIN_INPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};


static inline void igep0020_display_init(void)
{
	if ((gpio_request(GPIO_DVI_PUP, "DVI PUP") == 0) &&
	    (gpio_direction_output(GPIO_DVI_PUP, 1) == 0))
		gpio_export(GPIO_DVI_PUP, 0);
	else
		pr_err("IGEP: Could not obtain gpio DVI PUP\n");

	igep00x0_dvi_device.platform_enable = igep0020_enable_dvi;
	igep00x0_dvi_device.platform_disable = igep0020_disable_dvi;

	platform_device_register(&igep0020_dss_device);
}

/* Expansion board: IGEP0022 */
extern void __init igep0022_init(void);

static void __init igep0020_init(void)
{
	int opt = 0;	
	
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	/* Ensure msecure is mux'd to be able to set the RTC. */
	omap_mux_init_signal("sys_drm_msecure", OMAP_PIN_OFF_OUTPUT_HIGH);

	/* Get IGEP0020 Hardware Revision */
	igep0020_get_revision();

	/* Add twl4030 common data */
	omap3_pmic_get_config(&twl4030_pdata, TWL_COMMON_PDATA_USB |
			TWL_COMMON_PDATA_AUDIO | TWL_COMMON_PDATA_MADC,
			TWL_COMMON_REGULATOR_VDAC | TWL_COMMON_REGULATOR_VPLL2);

	igep00x0_pmic_get_config(&twl4030_pdata, 0,
			TWL_IGEP00X0_REGULATOR_VMMC1 |
			TWL_IGEP00X0_REGULATOR_VIO);

	twl4030_pdata.codec->audio->reset_registers = 1;

	omap3_pmic_init("twl4030", &twl4030_pdata);

	/*
	 * Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz
	 */
	omap_register_i2c_bus(3, 100, igep2_i2c3_boardinfo,
			 ARRAY_SIZE(igep2_i2c3_boardinfo));

	/* Display initialitzation */
	igep0020_display_init();

	/* Register spidev devices (depends on hardware revision)*/
	if (hwrev == IGEP2_BOARD_HWREV_B) {
		igep2_spidev_board_info.bus_num	= 1;
		igep2_spidev_board_info.chip_select = 3;
	}
	
	spi_register_board_info(&igep2_spidev_board_info, 1);

	omap_serial_init();

	platform_device_register(&igep00x0_vdd33_device);

	/* USB OTG & USB HOST */
	usb_musb_init(&igep00x0_musb_board_data);
	usb_ehci_init(&ehci_pdata);

	igep2_leds_init();

	/*
	 * By default UART1 is configured for RS485 usage, configure for
	 * RS232 if flag is enabled.
	 */
	mux_partition = omap_mux_get("core");
	if (igep0020_board_pdata.options & IGEP0020_BOARD_OPT_RS232)
		omap_mux_write_array(mux_partition, uart1_as_rs232_mux);

	#if defined(CONFIG_HAVE_PWM)
	omap_mux_write_array(mux_partition, nsc_as_pwm_mux);
	pwm_init();
	#endif

	/* Expansion board initialitzations */
	/* - IGEP0022 */
	if (igep00x0_buddy_pdata.model == IGEP00X0_BUDDY_IGEP0022)
		igep0022_init();

	/* Common initialitzations */
	/* - Register flash devices */
	igep00x0_flash_init();

	/* - Ethernet with SMSC9221 LAN Controller */
	igep00x0_smsc911x_init(&smsc911x_device, SMSC911X_CS,
			SMSC911X_IRQ, SMSC911X_NRESET);

	/*
	 * WLAN-BT combo module from MuRata with SDIO interface.
	 *
	 * NOTE: If we have an expansion board with modem enabled we need to
	 * disable the bluetooth interface as is INCOMPATIBLE
	 */
	// opt = igep00x0_buddy_pdata.options & IGEP00X0_BUDDY_OPT_MODEM;
	opt = 1; // disable BT so we can use UART
	if (hwrev == IGEP2_BOARD_HWREV_B)
		igep00x0_wifi_bt_init(IGEP2_RB_GPIO_WIFI_NPD,
			IGEP2_RB_GPIO_WIFI_NRESET, IGEP2_RB_GPIO_BT_NRESET,
			!opt);
	else if (hwrev == IGEP2_BOARD_HWREV_C)
		igep00x0_wifi_bt_init(IGEP2_RC_GPIO_WIFI_NPD,
			IGEP2_RC_GPIO_WIFI_NRESET, IGEP2_RC_GPIO_BT_NRESET,
			!opt);
}

static int __init ei485_early_param(char *str)
{
	char opt[16];

	if (!str)
		return -EINVAL;

	strncpy(opt, str, 16);

	/*
	 * To use UART1 as RS232 port instead of RS485 we need configure
	 * UART1_RTS and UART1_CTS pins in safe mode.
	 */
	if (!strcmp(opt, "no"))
		igep0020_board_pdata.options  |= IGEP0020_BOARD_OPT_RS232;

	return 0;
}

early_param("board.ei485", ei485_early_param);

MACHINE_START(IGEP0020, "IGEP0020 board")
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve	= omap_reserve,
	.init_irq	= igep00x0_init_irq,
	.init_machine	= igep0020_init,
	.timer		= &omap_timer,
MACHINE_END
