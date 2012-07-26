/*
 * ultrasonic.c
 *
 *  Created on: Jul 26, 2012
 *      Author: nazgee
 */

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

#define ultrasonic_US 		1000
#define ultrasonic_NAME		"ultrasonic-hc-sr04"
#define ultrasonic_MAX_DEVS	2

static int foo = 7;


module_param(foo, int, 0444);

static dev_t ultrasonic_dev_first; /* Allotted device number */
static struct class *ultrasonic_class; /* Tie with the device model */
static struct ultrasonic_dev {
	struct cdev cdev; /* The cdev structure */
	struct device* dev;
	char name[15]; /* Name of I/O region */
	struct pwm_device *pwm;
	int pos;
	int constraint_min;
	int constraint_max;
	int constraint_steps;
	int constraint_period;
	char input_buffer[32];	/* holds bytes to be written to device */
	struct rw_semaphore sem;/* mutual exclusion semaphore     */
} *ultrasonic_devs;

static int ultrasonic_open(struct inode *inode, struct file *filp)
{
	struct ultrasonic_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct ultrasonic_dev, cdev);
	filp->private_data = dev; /* for other methods */

	return 0; /* success */
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
	snprintf(tmp, sizeof(tmp), "%d\n", dev->pos );
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

static ssize_t ultrasonic_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	int i, rc, value;

	struct ultrasonic_dev *dev = filp->private_data;	/* set up on ultrasonic_open() */
	char* read_from, *read_to = NULL;
	int read_count, safe_count;

	down_write(&dev->sem);

	read_from = dev->input_buffer;
	/* number of bytes that were read (used or dsicarded) */
	safe_count = count > sizeof(dev->input_buffer) ? sizeof(dev->input_buffer) - 1 : count;

	dev->input_buffer[safe_count] = 0; /* safety guard */

	if ( copy_from_user(dev->input_buffer, buf, safe_count) ) {
		rc = -EFAULT;
		goto out;
	}

	/* accept only numbers */
	for (i = 0; i < safe_count; i++) {
		if  (	(	( dev->input_buffer[i] >= '0' )	&&
					( dev->input_buffer[i] <= '9' )	) ||
					( dev->input_buffer[i] == '-' )	)
			break;
	}

	read_count = i;
	read_from = &dev->input_buffer[i];

	value = simple_strtol(read_from, &read_to, 10);
	if (read_from == read_to) {
		rc = safe_count;	/* no useful data in buffer. discard it*/
		goto out;
	};
	//ultrasonic_set_pos(dev, value, false);

	read_count += read_to - read_from;
	rc = read_count;
out:
	up_write(&dev->sem);
	return rc;
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations ultrasonic_fops = { .owner = THIS_MODULE, /* Owner */
		  .open     =   ultrasonic_open,        /* Open method */
		  .read     =   ultrasonic_read,        /* Read method */
		  .write    =   ultrasonic_write,       /* Write method */
};

static ssize_t sysfs_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ultrasonic_dev *ultrasonic = dev_get_drvdata(dev);
	ssize_t	status;
	down_read(&ultrasonic->sem);
	status = sprintf(buf, "%d\n", ultrasonic->constraint_max);
	up_read(&ultrasonic->sem);
	return status;
}

static ssize_t sysfs_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ultrasonic_dev *ultrasonic = dev_get_drvdata(dev);
	ssize_t	status;
	long value;

	status = strict_strtol(buf, 0, &value);
	if (status < 0)
		goto done;

	down_write(&ultrasonic->sem);
	ultrasonic->constraint_max = value;
//	ultrasonic_refresh_pos(ultrasonic);
	up_write(&ultrasonic->sem);
done:
	return status ? : size;
}

static DEVICE_ATTR(max, 0644, sysfs_max_show, sysfs_max_store);

static const struct attribute *ultrasonic_attrs[] = {
	&dev_attr_max.attr,
	NULL,
};

static const struct attribute_group ultrasonic_attr_group = {
	.attrs = (struct attribute **) ultrasonic_attrs,
};

static void cleanup_pwm(void)
{
//	int i;
//	for (i = 0; i < pwms_count; i++) {
//		pwm_disable(ultrasonic_devs[i].pwm);
//		pwm_free(ultrasonic_devs[i].pwm);
//	}
}

static void cleanup_cdev(void)
{
//	int i;
//	for (i = 0; i < pwms_count; i++) {
//		cdev_del(&ultrasonic_devs[i].cdev);
//	}
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void)
{
	int i, retval = 0;

	/* sanitize module's arguments */
	//TODO:

	/* Request dynamic allocation of a device major number */
//	if (alloc_chrdev_region(&ultrasonic_dev_first, 0, pwms_count, ultrasonic_NAME)
//			< 0) {
//		printk(KERN_ERR ultrasonic_NAME ": Can't register device\n");
//		return -EEXIST;
//	}

	/* Populate sysfs entries */
	ultrasonic_class = class_create(THIS_MODULE, ultrasonic_NAME);

//	/* Allocate memory for the per-device structure */
//	ultrasonic_devs = kmalloc(sizeof(struct ultrasonic_dev) * pwms_count, GFP_KERNEL);
//	if (!ultrasonic_devs) {
//		printk(KERN_ERR ultrasonic_NAME ": Bad kmalloc\n");
//
//		retval = -ENOMEM;
//		goto error_destroy_class;
//	}
//
//	for (i = 0; i < pwms_count; i++) {
//		/* Connect the file operations with the cdev */
//		cdev_init(&ultrasonic_devs[i].cdev, &ultrasonic_fops);
//		ultrasonic_devs[i].cdev.owner = THIS_MODULE;
//		ultrasonic_devs[i].constraint_max = max_us[i];
//		ultrasonic_devs[i].constraint_min = min_us[i];
//		ultrasonic_devs[i].constraint_period = period_us[i];
//		ultrasonic_devs[i].constraint_steps = steps[i];
//		ultrasonic_devs[i].pos = 0;
//
//		sprintf(ultrasonic_devs[i].name, ultrasonic_NAME "-pwm%d", i);
//		ultrasonic_devs[i].pwm = pwm_request(pwms_id[i], ultrasonic_devs[i].name);
//
//		if (IS_ERR(ultrasonic_devs[i].pwm)) {
//			int j;
//			printk(KERN_ERR ultrasonic_NAME ": pwm%d is not free.\n", i);
//			for (j = 0; j < i; j++) {
//				pwm_free(ultrasonic_devs[j].pwm);
//			}
//
//			retval = -EIO;
//			goto error_free_malloc;
//		}
//		init_rwsem( &ultrasonic_devs[i].sem );
//	}
//
//	for (i = 0; i < pwms_count; i++) {
//		/* Connect the major/minor number to the cdev */
//		if (cdev_add(&ultrasonic_devs[i].cdev, (ultrasonic_dev_first + i), 1)) {
//			int j;
//			printk(KERN_ERR ultrasonic_NAME ": Bad cdev @%d\n", i);
//
//			for (j = 0; j < i; j++) {
//				cdev_del(&ultrasonic_devs[j].cdev);
//			}
//			retval = -EIO;
//			goto error_cleanup_pwm;
//		}
//	}
//
//	for (i = 0; i < pwms_count; i++) {
//		/* Send uevents to udev, so it'll create /dev nodes */
//		struct device* d = device_create(ultrasonic_class, NULL,
//				(ultrasonic_dev_first + i), &ultrasonic_devs[i], ultrasonic_NAME"%d", i);
//
//		if (IS_ERR(d)) {
//			int j;
//			printk(KERN_ERR ultrasonic_NAME ": Bad cdev @%d\n", i);
//
//			for (j = 0; j < i; j++) {
//				cdev_del(&ultrasonic_devs[j].cdev);
//			}
//			retval = PTR_ERR(d);
//			goto error_cleanup_cdev;
//		}
//
//		ultrasonic_devs[i].dev = d;
//
//		if ((retval = sysfs_create_group(&d->kobj, &ultrasonic_attr_group))) {
//			printk(KERN_ERR ultrasonic_NAME ": error creating sysfs group\n");
//		}
//
//		ultrasonic_refresh_all(&ultrasonic_devs[i]);
//	}

	return 0;

//error_cleanup_cdev:
//	cleanup_cdev();
//error_cleanup_pwm:
//	cleanup_pwm();
//error_free_malloc:
//	kfree(ultrasonic_devs);
//error_destroy_class:
//	class_destroy(ultrasonic_class);
//	unregister_chrdev_region(MAJOR(ultrasonic_dev_first), pwms_count);
//	return retval;
}

/* Driver Exit */
void __exit
ultrasonic_cleanup(void)
{
	int i;
	/* Release the major number */
//	unregister_chrdev_region(MAJOR(ultrasonic_dev_first), pwms_count);

//	for (i = 0; i < pwms_count; i++) {
//		/* Remove the cdev */
//		cdev_del(&ultrasonic_devs[i].cdev);
//		dev_dbg(ultrasonic_devs[i].dev, "destroying...");
//		/* tell udev that /dev nodes should be removed */
//		device_destroy(ultrasonic_class, MKDEV(MAJOR(ultrasonic_dev_first), i));
//	}
	cleanup_pwm();
	kfree(ultrasonic_devs);
	class_destroy(ultrasonic_class);
//	unregister_chrdev_region(MAJOR(ultrasonic_dev_first), pwms_count);
	return;
}

module_init(ultrasonic_init)
module_exit(ultrasonic_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2012-07-26");

