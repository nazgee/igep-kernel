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

struct ultrasonic_dev;

struct ultrasonic_operations {
	int (*measure) (struct ultrasonic_dev* ultrasonic_dev);
	int (*open) (struct inode *, struct file *, struct ultrasonic_dev* udev);
};

struct ultrasonic_dev {
	struct cdev cdev; /* The cdev structure */
	struct device* dev;
	char name[15]; /* Name of I/O region */
	struct ultrasonic_operations* ops;
	struct rw_semaphore sem; /* mutual exclusion semaphore */
};

int ultrasonic_register_device(struct ultrasonic_dev *dev, char* name);

#endif /* ultrasonic */
