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
#include <linux/semaphore.h>
#include <asm/uaccess.h>	/* copy_*_user */
//#define DEBUG
#include <linux/device.h> //class_create
#include <string.h>

#define ULTRASONIC_NAME		"ultrasonic"


static dev_t ultrasonic_dev_first; /* Allotted device number */
static int ultrasonic_major = 0;
static int ultrasonic_devices = 0;
static struct class* ultrasonic_class;
static DEFINE_SEMAPHORE(biglock);

static int ultrasonic_open(struct inode *inode, struct file *filp)
{
	struct ultrasonic_dev *dev; /* device information */
	dev = container_of(inode->i_cdev, struct ultrasonic_dev, cdev);

	return dev->ops->open(inode, filp, dev);
}

static ssize_t ultrasonic_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	int rc;
	struct ultrasonic_dev *dev = filp->private_data;
	char tmp[14];	/* 32bit value with sign + '\n' + '0' */

	if (*f_pos)	{
		rc = 0;
		goto out;
	}
	BUG_ON(count < sizeof(tmp) );

	down_read(&dev->sem);
	int distance = dev->ops->measure(dev);
	snprintf(tmp, sizeof(tmp), "%d\n", distance );
	up_read(&dev->sem);

	rc = strlen(tmp)+1;
	*f_pos += rc;

	if ( copy_to_user(buf, tmp, rc) ) {
		rc = -EFAULT;
		goto out;
	}

out:
	return rc;
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations ultrasonic_fops = { .owner = THIS_MODULE, /* Owner */
		  .open     =   ultrasonic_open,        /* Open method */
		  .read     =   ultrasonic_read,        /* Read method */
};

int ultrasonic_register_device(struct ultrasonic_dev *dev, char* name)
{
	int err;
	char nodename[64] = ULTRASONIC_NAME;
	strncat(nodename, name, 64);

	down(biglock);

	if (ultrasonic_major == 0) {
		/* Request dynamic allocation of a device major number */
		err = alloc_chrdev_region(&ultrasonic_dev_first, 0, 1, ULTRASONIC_NAME);
		if (err < 0) {
			printk("pg_init: unable to allocate major number for %s!\n", nodename);
			goto out;
		}
		ultrasonic_major = MAJOR(ultrasonic_dev_first);

		/* Create device class (before allocation of the array of devices) */
		ultrasonic_class = class_create(THIS_MODULE, ULTRASONIC_NAME);
		if (IS_ERR(ultrasonic_class)) {
			err = PTR_ERR(ultrasonic_class);
			goto out;
		}

	} else {

	}

out:
	up(biglock);
	return err;
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void)
{
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
