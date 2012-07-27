/*
 * ultrasonic.h
 *
 *  Created on: Feb 13, 2011
 *      Author: nazgee
 */

#ifndef ULTRASONIC_H_
#define ULTRASONIC_H_

#include <linux/cdev.h>	//struct cdev
#include <linux/rwsem.h>

struct ultrasonic_drv;

struct ultrasonic_dev {
	struct cdev cdev; /* The cdev structure */
	struct device* device;
	char name[15]; /* Name of I/O region */
	struct ultrasonic_drv* driver;
	struct rw_semaphore sem; /* mutual exclusion semaphore */
};

struct ultrasonic_drv {
	struct ultrasonic_dev* udev;
	int (*measure) (struct ultrasonic_drv* udrv);
};

int ultrasonic_register_driver(struct ultrasonic_drv *dev, char* name);
int ultrasonic_unregister_driver(struct ultrasonic_drv *udrv);

#endif /* ultrasonic */
