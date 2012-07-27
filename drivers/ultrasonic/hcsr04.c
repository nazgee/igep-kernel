/*
 * ultrasonic.c
 *
 *  Created on: Jul 26, 2012
 *      Author: nazgee
 */

#include <linux/ultrasonic/ultrasonic.h>
#include <linux/fs.h>
#include <linux/cdev.h>	  //struct cdev
#include <linux/module.h> //THIS_MODULE
#include <linux/io.h>
#include <linux/slab.h>	//kmalloc
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <asm/uaccess.h>	/* copy_*_user */
//#define DEBUG
#include <linux/device.h> //class_create

#define ULTRASONIC_NAME		"hcsr04"

//static int gpio_trigger = 7;
//module_param(gpio_trigger, int, 0444);
//static int gpio_echo = 7;
//module_param(gpio_echo, int, 0444);

struct hcsr04_dev {
	struct ultrasonic_drv ultrasonic;
	int gpio_trigger;
	int gpio_echo;
};

static int hcsr04_read(struct ultrasonic_drv* udrv);

static struct hcsr04_dev dev= {
	.ultrasonic = {
		.measure = hcsr04_read
	},
	.gpio_trigger = -1,
	.gpio_echo = -1
};

int hcsr04_read(struct ultrasonic_drv* udrv) {
	return 666;
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void)
{
	return ultrasonic_register_driver(&dev.ultrasonic, ULTRASONIC_NAME);
}

/* Driver Exit */
void __exit
ultrasonic_cleanup(void)
{
	ultrasonic_unregister_driver(&dev.ultrasonic);
	return;
}

module_init(ultrasonic_init)
module_exit(ultrasonic_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2012-07-26");

