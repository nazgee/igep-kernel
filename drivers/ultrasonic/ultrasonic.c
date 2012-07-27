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
#include <linux/types.h> //dev_t
#include <linux/string.h>

#define ULTRASONIC_NAME		"ultrasonic"


static void ultrasonic_cleanup_class(void);
static int ultrasonic_open(struct inode *inode, struct file *filp);
static ssize_t ultrasonic_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos);

struct ultrasonic_context {
	dev_t first_dev;
	int major;
	int devices_number;
	struct class* class;
	struct semaphore biglock;

	/* File operations structure. Defined in linux/fs.h */
	struct file_operations fops;
};

static struct ultrasonic_context ultrasonic = {
	.first_dev = 0,
	.major = 0,
	.devices_number = 0,
	.class = NULL,
	.fops = {
		.owner = THIS_MODULE,		/* Owner */
		.open = ultrasonic_open,	/* Open method */
		.read = ultrasonic_read,	/* Read method */
	}
};


int ultrasonic_open(struct inode *inode, struct file *filp)
{
	struct ultrasonic_dev *dev; /* device information */
	dev = container_of(inode->i_cdev, struct ultrasonic_dev, cdev);
	filp->private_data = dev; /* for other fops methods*/

	return 0;
}

ssize_t ultrasonic_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	int rc, distance;
	struct ultrasonic_dev *dev = filp->private_data;
	char tmp[14];	/* 32bit value with sign + '\n' + '0' */

	if (*f_pos)	{
		rc = 0;
		goto out;
	}
	BUG_ON(count < sizeof(tmp) );

	down_read(&dev->sem);
	distance = dev->driver->measure(dev->driver);
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


int ultrasonic_register_driver(struct ultrasonic_drv *udrv, char* name)
{
	struct ultrasonic_dev* udev;
	struct device* d;
	int err = 0;

	// FIXME this is obviously wrong when devs are removed- devs shall be kept
	// on a list that should be traversed to get a free minor number
	int devminor = ultrasonic.devices_number;

	down(&ultrasonic.biglock);

	/* Allocate memory for the per-device structure */
	udev = kmalloc(sizeof(struct ultrasonic_dev), GFP_KERNEL);
	if (!udev) {
		printk(KERN_ERR ULTRASONIC_NAME": bad kmalloc\n");
		err = -ENOMEM;
		goto out;
	}

	/* Connect the file operations with cdev */
	cdev_init(&udev->cdev, &ultrasonic.fops);
	udev->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number with cdev */
	err = cdev_add(&udev->cdev, ultrasonic.major + devminor, 1);
	if (err) {
		printk(KERN_ERR ULTRASONIC_NAME": failed to add character device (%d) for %s\n", err, name);
		goto error_cleanup_alloc;
	}

	/* Send uevents to udev, so it'll create /dev nodes */
	d = device_create(ultrasonic.class, NULL, udev->cdev.dev, udev,
			ULTRASONIC_NAME".%d.%s.", devminor, name);
	if (IS_ERR(d)) {
		printk(KERN_ERR ULTRASONIC_NAME": failed to add character device (%d) for %s\n", err, name);
		err = PTR_ERR(d);
		goto error_cleanup_cdev_add;
	}
	udev->device = d;

	printk(KERN_ERR ULTRASONIC_NAME": installed new drv %s (%d:%d)\n", name, MAJOR(udev->cdev.dev), MINOR(udev->cdev.dev));
	ultrasonic.devices_number++;

	// Match device and driver
	udrv->udev = udev;
	udev->driver = udrv;
	goto out;

	error_cleanup_cdev_add:
		cdev_del(&udev->cdev);
	error_cleanup_alloc:
		kfree(udev);
out:
	up(&ultrasonic.biglock);
	return err;
}

int ultrasonic_unregister_driver(struct ultrasonic_drv *udrv)
{
	struct ultrasonic_dev *udev;

	BUG_ON(IS_ERR_OR_NULL(udrv));
	BUG_ON(ultrasonic.devices_number <= 0);

	udev = udrv->udev;
	down(&ultrasonic.biglock);

	ultrasonic.devices_number--;
	device_destroy(ultrasonic.class, udev->cdev.dev);
	cdev_del(&udev->cdev);
	kfree(udev);

	up(&ultrasonic.biglock);
	return 0;
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void)
{
	int err = 0;
	sema_init(&ultrasonic.biglock, 1);

	/* Request dynamic allocation of a device major number */
	err = alloc_chrdev_region(&ultrasonic.first_dev, 0, 256, ULTRASONIC_NAME);
	if (err < 0) {
		printk(KERN_ERR ULTRASONIC_NAME": unable to allocate major number!\n");
		goto out;
	}
	ultrasonic.major = MAJOR(ultrasonic.first_dev);

	/* Create device class */
	ultrasonic.class = class_create(THIS_MODULE, ULTRASONIC_NAME);
	if (IS_ERR(ultrasonic.class)) {
		printk(KERN_ERR ULTRASONIC_NAME": class_create failed!\n");
		err = PTR_ERR(ultrasonic.class);
		goto error_cleanup_alloc_chrdev;
	}

	printk(KERN_ERR ULTRASONIC_NAME": my major=%d!\n", ultrasonic.major);
	goto out;

error_cleanup_alloc_chrdev:
	unregister_chrdev_region(ultrasonic.major, 256);
out:
	return err;
}

/* Driver Exit */
void __exit
ultrasonic_cleanup(void)
{
	class_destroy(ultrasonic.class);
	unregister_chrdev_region(ultrasonic.major, 256);
	return;
}

/* Let modules register as our drivers */
EXPORT_SYMBOL(ultrasonic_register_driver);
EXPORT_SYMBOL(ultrasonic_unregister_driver);

module_init(ultrasonic_init)
module_exit(ultrasonic_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2012-07-26");
