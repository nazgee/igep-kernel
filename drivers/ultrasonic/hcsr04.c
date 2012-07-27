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

static struct class *ultrasonic_class; /* Tie with the device model */
static struct hcsr04_dev {
	struct ultrasonic_dev ultrasonic;
	int gpio_trigger;
	int gpio_echo;
} *hcsr04_devs;

static int hcsr04_open(struct inode *inode, struct file *filp,
		struct ultrasonic_dev* udev)
{
	struct hcsr04_dev *dev = udev;
	filp->private_data = dev; /* for other methods */
	return 0; /* success */
}

static int hcsr04_read() {
	return 666;
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void)
{
	ultrasonic_register_device(NULL, "foo");
	return 0;
}

/* Driver Exit */
void __exit
ultrasonic_cleanup(void)
{
	return;
}

module_init(ultrasonic_init)
module_exit(ultrasonic_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2012-07-26");

