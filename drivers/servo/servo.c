/*
 * servo.c
 *
 *  Created on: Feb 13, 2011
 *      Author: nazgee
 */

#include <linux/fs.h>
#include <linux/cdev.h>	  //struct cdev
#include <linux/module.h> //THIS_MODULE
#include <linux/io.h>
#include <linux/slab.h>	//kmalloc
#include <linux/moduleparam.h>
#include <linux/servo/servo.h>
#include <linux/pwm.h>
#include <linux/stat.h>
#include <asm/uaccess.h>	/* copy_*_user */
//#define DEBUG
#include <linux/device.h> //class_create

#define SERVO_US 		1000
#define SERVO_NAME		"servodrive"
#define SERVO_MAX_DEVS	2

static int pwms_count = SERVO_MAX_DEVS;
static int pwms_id[SERVO_MAX_DEVS] = {
	0,
	1
};
static int mins_count = SERVO_MAX_DEVS;
static int min_us[SERVO_MAX_DEVS] = {
	850,
	850
};
static int maxs_count = SERVO_MAX_DEVS;
static int max_us[SERVO_MAX_DEVS] = {
	2150,
	2150
};
static int periods_count = SERVO_MAX_DEVS;
static int period_us[SERVO_MAX_DEVS] = {
	20000,
	20000
};
static int steps_count = SERVO_MAX_DEVS;
static int steps[SERVO_MAX_DEVS] = {
	1024,
	1024
};

module_param_array(min_us, int, &mins_count, 0444);
module_param_array(max_us, int, &maxs_count, 0444);
module_param_array(period_us, int, &periods_count, 0444);
module_param_array(steps, int, &steps_count, 0444);

static dev_t servo_dev_first; /* Allotted device number */
static struct class *servo_class; /* Tie with the device model */
static struct servo_dev {
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
} *servo_devs;

static void servo_set_pos(struct servo_dev *dev, int pos, int force)
{
	int step_min, step_max, pos_us;
	if (dev->constraint_steps % 2) {
		step_max = dev->constraint_steps/2;
		step_min = -step_max;
	} else {
		step_min = -dev->constraint_steps/2;
		step_max = -step_min - 1;
	}
	pos = clamp(pos, step_min, step_max);

	if (!force && (pos == dev->pos))
		return;

	pos_us = dev->constraint_min + (pos - step_min)
			* (dev->constraint_max - dev->constraint_min)
			/ (dev->constraint_steps);

	if (!force) {
		pwm_modify_duty(dev->pwm, pos_us * SERVO_US);
	} else {
		pwm_config(dev->pwm, pos_us * SERVO_US, dev->constraint_period * SERVO_US);
		pwm_enable(dev->pwm);
	}
	dev->pos = pos;
	dev_dbg(dev->dev, "new pos=%d @ range={%dus..%dus}", dev->pos, dev->constraint_min, dev->constraint_max);
}

static void servo_refresh_pos(struct servo_dev *dev)
{
	servo_set_pos(dev, dev->pos, false);
}

static void servo_refresh_all(struct servo_dev *dev)
{
	servo_set_pos(dev, dev->pos, true);
}

static int servo_open(struct inode *inode, struct file *filp)
{
	struct servo_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct servo_dev, cdev);
	filp->private_data = dev; /* for other methods */

	return 0; /* success */
}

static ssize_t servo_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	int rc;
	struct servo_dev *dev = filp->private_data;
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

static ssize_t servo_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	int i, rc, value;

	struct servo_dev *dev = filp->private_data;	/* set up on servo_open() */
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
	servo_set_pos(dev, value, false);

	read_count += read_to - read_from;
	rc = read_count;
out:
	up_write(&dev->sem);
	return rc;
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations servo_fops = { .owner = THIS_MODULE, /* Owner */
		  .open     =   servo_open,        /* Open method */
		  .read     =   servo_read,        /* Read method */
		  .write    =   servo_write,       /* Write method */
};

enum servo_constraints {
	CONSTRAINT_MIN,
	CONSTRAINT_MAX,
	CONSTRAINT_STEPS
};

static ssize_t sysfs_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	down_read(&servo->sem);
	status = sprintf(buf, "%d\n", servo->constraint_max);
	up_read(&servo->sem);
	return status;
}

static ssize_t sysfs_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	down_read(&servo->sem);
	status = sprintf(buf, "%d\n", servo->constraint_min);
	up_read(&servo->sem);
	return status;
}

static ssize_t sysfs_steps_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	down_read(&servo->sem);
	status = sprintf(buf, "%d\n", servo->constraint_steps);
	up_read(&servo->sem);
	return status;
}

static ssize_t sysfs_period_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	down_read(&servo->sem);
	status = sprintf(buf, "%d\n", servo->constraint_period);
	up_read(&servo->sem);
	return status;
}

static ssize_t sysfs_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	long value;

	status = strict_strtol(buf, 0, &value);
	if (status < 0)
		goto done;

	down_write(&servo->sem);
	servo->constraint_max = value;
	servo_refresh_pos(servo);
	up_write(&servo->sem);
done:
	return status ? : size;
}

static ssize_t sysfs_min_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	long value;

	status = strict_strtol(buf, 0, &value);
	if (status < 0)
		goto done;

	down_write(&servo->sem);
	servo->constraint_min = value;
	servo_refresh_pos(servo);
	up_write(&servo->sem);
done:
	return status ? : size;
}

static ssize_t sysfs_steps_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	long value;

	status = strict_strtol(buf, 0, &value);
	if (status < 0)
		goto done;

	down_write(&servo->sem);
	servo->constraint_steps = value;
	servo_refresh_pos(servo);
	up_write(&servo->sem);
done:
	return status ? : size;
}

static ssize_t sysfs_period_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct servo_dev *servo = dev_get_drvdata(dev);
	ssize_t	status;
	long value;

	status = strict_strtol(buf, 0, &value);
	if (status < 0)
		goto done;

	down_write(&servo->sem);
	servo->constraint_period = value;
	servo_refresh_all(servo);
	up_write(&servo->sem);
done:
	return status ? : size;
}

static DEVICE_ATTR(max, 0644, sysfs_max_show, sysfs_max_store);
static DEVICE_ATTR(min, 0644, sysfs_min_show, sysfs_min_store);
static DEVICE_ATTR(steps, 0644, sysfs_steps_show, sysfs_steps_store);
static DEVICE_ATTR(period, 0644, sysfs_period_show, sysfs_period_store);

static const struct attribute *servo_attrs[] = {
	&dev_attr_max.attr,
	&dev_attr_min.attr,
	&dev_attr_steps.attr,
	&dev_attr_period.attr,
	NULL,
};

static const struct attribute_group servo_attr_group = {
	.attrs = (struct attribute **) servo_attrs,
};

static void cleanup_pwm(void)
{
	int i;
	for (i = 0; i < pwms_count; i++) {
		pwm_disable(servo_devs[i].pwm);
		pwm_free(servo_devs[i].pwm);
	}
}

static void cleanup_cdev(void)
{
	int i;
	for (i = 0; i < pwms_count; i++) {
		cdev_del(&servo_devs[i].cdev);
	}
}

/*
 * Driver Initialization
 */
int __init servo_init(void)
{
	int i, retval = 0;

	/* sanitize module's arguments */
	//TODO:

	/* Request dynamic allocation of a device major number */
	if (alloc_chrdev_region(&servo_dev_first, 0, pwms_count, SERVO_NAME)
			< 0) {
		printk(KERN_ERR SERVO_NAME ": Can't register device\n");
		return -EEXIST;
	}

	/* Populate sysfs entries */
	servo_class = class_create(THIS_MODULE, SERVO_NAME);

	/* Allocate memory for the per-device structure */
	servo_devs = kmalloc(sizeof(struct servo_dev) * pwms_count, GFP_KERNEL);
	if (!servo_devs) {
		printk(KERN_ERR SERVO_NAME ": Bad kmalloc\n");

		retval = -ENOMEM;
		goto error_destroy_class;
	}

	for (i = 0; i < pwms_count; i++) {
		/* Connect the file operations with the cdev */
		cdev_init(&servo_devs[i].cdev, &servo_fops);
		servo_devs[i].cdev.owner = THIS_MODULE;
		servo_devs[i].constraint_max = max_us[i];
		servo_devs[i].constraint_min = min_us[i];
		servo_devs[i].constraint_period = period_us[i];
		servo_devs[i].constraint_steps = steps[i];
		servo_devs[i].pos = 0;

		sprintf(servo_devs[i].name, SERVO_NAME "-pwm%d", i);
		servo_devs[i].pwm = pwm_request(pwms_id[i], servo_devs[i].name);

		if (IS_ERR(servo_devs[i].pwm)) {
			int j;
			printk(KERN_ERR SERVO_NAME ": pwm%d is not free.\n", i);
			for (j = 0; j < i; j++) {
				pwm_free(servo_devs[j].pwm);
			}

			retval = -EIO;
			goto error_free_malloc;
		}
		init_rwsem( &servo_devs[i].sem );
	}

	for (i = 0; i < pwms_count; i++) {
		/* Connect the major/minor number to the cdev */
		if (cdev_add(&servo_devs[i].cdev, (servo_dev_first + i), 1)) {
			int j;
			printk(KERN_ERR SERVO_NAME ": Bad cdev @%d\n", i);

			for (j = 0; j < i; j++) {
				cdev_del(&servo_devs[j].cdev);
			}
			retval = -EIO;
			goto error_cleanup_pwm;
		}
	}

	for (i = 0; i < pwms_count; i++) {
		/* Send uevents to udev, so it'll create /dev nodes */
		struct device* d = device_create(servo_class, NULL,
				(servo_dev_first + i), &servo_devs[i], SERVO_NAME"%d", i);

		if (IS_ERR(d)) {
			int j;
			printk(KERN_ERR SERVO_NAME ": Bad cdev @%d\n", i);

			for (j = 0; j < i; j++) {
				cdev_del(&servo_devs[j].cdev);
			}
			retval = PTR_ERR(d);
			goto error_cleanup_cdev;
		}

		servo_devs[i].dev = d;

		if ((retval = sysfs_create_group(&d->kobj, &servo_attr_group))) {
			printk(KERN_ERR SERVO_NAME ": error creating sysfs group\n");
		}

		servo_refresh_all(&servo_devs[i]);
	}

	return 0;

error_cleanup_cdev:
	cleanup_cdev();
error_cleanup_pwm:
	cleanup_pwm();
error_free_malloc:
	kfree(servo_devs);
error_destroy_class:
	class_destroy(servo_class);
	unregister_chrdev_region(MAJOR(servo_dev_first), pwms_count);
	return retval;
}

/* Driver Exit */
void __exit
servo_cleanup(void)
{
	int i;
	/* Release the major number */
	unregister_chrdev_region(MAJOR(servo_dev_first), pwms_count);

	for (i = 0; i < pwms_count; i++) {
		/* Remove the cdev */
		cdev_del(&servo_devs[i].cdev);
		dev_dbg(servo_devs[i].dev, "destroying...");
		/* tell udev that /dev nodes should be removed */
		device_destroy(servo_class, MKDEV(MAJOR(servo_dev_first), i));
	}
	cleanup_pwm();
	kfree(servo_devs);
	class_destroy(servo_class);
	unregister_chrdev_region(MAJOR(servo_dev_first), pwms_count);
	return;
}

module_init(servo_init)
module_exit(servo_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2011-02-17");

