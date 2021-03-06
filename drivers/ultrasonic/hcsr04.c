/*
 * ultrasonic.c
 *
 *  Created on: Jul 26, 2012
 *      Author: nazgee
 */

#include <linux/ultrasonic/ultrasonic.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>

#define HARDWARE_NAME	"hcsr04"
#define HSCR04_TRIG_LEVEL 1
#define HSCR04_ECHO_LEVEL 1
#define HSCR04_NO_OBSTACLE_US 38000
#define HSCR04_MEASUREMENT_TIMEOUT_MS 250
#define HSCR04_TRIGGER_US 10
#define HSCR04_ECHO_FADE_MIN_US 20000
#define HSCR04_ECHO_FADE_US 50000
#define HSCR04_US_TO_CM_DIVISOR 58


static int hcsr04_read(struct ultrasonic_drv* udrv);

static int inverse_trigger = 0;
static int inverse_echo = 0;
static int gpio_trig = -EINVAL;
static int gpio_echo = -EINVAL;

module_param(inverse_trigger, int, 0444);
module_param(inverse_echo, int, 0444);
module_param(gpio_trig, int, 0444);
module_param(gpio_echo, int, 0444);

struct hcsr04_drv {
	struct ultrasonic_drv udrv;
	int id;
	int irq_echo;
	int gpio_trig;
	int gpio_echo;
	int inverse_trig;
	int inverse_echo;
	struct timespec timestamp_trigger;
	volatile int ping_us;
	volatile int ping_trigger;
	volatile int ping_rxed;
	wait_queue_head_t wq_echo;
	struct semaphore sem; /* locks against simultaneous measurements */
};

static struct hcsr04_drv drv = {
	.udrv = {
		.measure = hcsr04_read
	},
	.irq_echo = -EINVAL,
	.gpio_trig = -EINVAL,
	.gpio_echo = -EINVAL,
	.inverse_trig = 0,
	.inverse_echo = 0
};

static int hcsr04_get_level_trigger(struct hcsr04_drv* drv, int is_triggered) {
	return is_triggered ^ drv->inverse_trig;
}

static int hcsr04_set_echo_edge(struct hcsr04_drv* drv, int is_echo_expected) {
	return set_irq_type(drv->irq_echo, (is_echo_expected ^ drv->inverse_echo) ?
			IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING);
}

static int hcsr04_us_from_trigger(struct hcsr04_drv *dev) {
	struct timespec now, diff;
	do_posix_clock_monotonic_gettime(&now);
	diff = timespec_sub(now, dev->timestamp_trigger);
	return diff.tv_sec * USEC_PER_SEC + diff.tv_nsec / NSEC_PER_USEC;
}

static irqreturn_t hcsr04_echo_isr(int irq, void *cookie)
{
	struct  hcsr04_drv *dev = (struct hcsr04_drv*)cookie;
	int us;

	if (dev->ping_trigger) {
		hcsr04_set_echo_edge(dev, 0);
		do_posix_clock_monotonic_gettime(&dev->timestamp_trigger);
		dev->ping_trigger = 0;
	} else {
		us = hcsr04_us_from_trigger(dev);
		if (us > HSCR04_US_TO_CM_DIVISOR) {
			dev->ping_rxed = 1;
			dev->ping_us = us;
			wake_up(&dev->wq_echo);
		}
	}

	return IRQ_HANDLED;
}

static int hcsr04_request_gpio(int gpio, int id, char* label) {
	int err = 0;
	char name[32];
	sprintf(name, "%s.%d.%s", HARDWARE_NAME, id, label);

	err = gpio_request(gpio, name);
	if (err) {
		printk(KERN_ERR "gpio_request failed for %s (pin %d)\n", name, gpio);
		goto out;
	}

	if (gpio_cansleep(gpio)) {
		err = -EIO;
		printk(KERN_ERR "given gpio can sleep- this is NOT acceptable!\n");
		goto error_cleanup_request;
	}

	goto out;

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

static int hcsr04_request_echo(struct hcsr04_drv* drv) {
	int err = 0;
	int gpio = drv->gpio_echo;

	err = hcsr04_request_gpio(gpio, drv->id, "echo");
	if (err) {
		goto out;
	}

	err = gpio_direction_input(gpio);
	if (err) {
		printk(KERN_ERR "could not set direction of gpio%d to input!\n", gpio);
		goto error_cleanup_request;
	}

	drv->irq_echo = gpio_to_irq(gpio);
	if (drv->irq_echo < 0) {
		printk(KERN_ERR "could not map gpio%d to irq!\n", gpio);
		err = drv->irq_echo;
		goto error_cleanup_request;
	}

	err = request_irq(drv->irq_echo, hcsr04_echo_isr, IRQF_TRIGGER_FALLING, HARDWARE_NAME"_echo", drv);
	if (err < 0) {
		printk(KERN_ERR "request_irq() failed: %d\n", err);
		goto error_cleanup_request;
	}

	disable_irq_nosync(drv->irq_echo);

	goto out;

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

static int hcsr04_request_trig(struct hcsr04_drv* drv) {
	int value, err = 0;
	int gpio = drv->gpio_trig;

	err = hcsr04_request_gpio(gpio, drv->id, "trig");
	if (err) {
		goto out;
	}

	value = hcsr04_get_level_trigger(drv, 0);
	printk(KERN_ERR "trigger@%d is off (%d)\n", gpio, value);
	err = gpio_direction_output(gpio, value);
	if (err) {
		printk(KERN_ERR "could not set direction of gpio%d to output!\n", gpio);
		goto error_cleanup_request;
	}

	goto out;

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

static int hcsr04_is_us_valid(int ping_us) {
	return (ping_us < HSCR04_NO_OBSTACLE_US);
}

static int hcsr04_cm_from_us(int ping_us) {
	if (!hcsr04_is_us_valid(ping_us)) {
		return -EINVAL;
	} else {
		return ping_us / HSCR04_US_TO_CM_DIVISOR;
	}
}

void hcsr04_free_echo(struct hcsr04_drv* drv) {
	free_irq(drv->irq_echo, drv);
	gpio_free(drv->gpio_echo);
}

void hcsr04_free_trig(struct hcsr04_drv* drv) {
	gpio_free(drv->gpio_trig);
}

int hcsr04_register(struct hcsr04_drv* drv) {
	int err;

	err = hcsr04_request_echo(drv);
	if (err) {
		goto out;
	}

	err = hcsr04_request_trig(drv);
	if (err) {
		goto error_cleanup_echo;
	}

	err = ultrasonic_register_device(&drv->udrv, HARDWARE_NAME);
	if (err) {
		goto error_cleanup_trig;
	}

	goto out;

error_cleanup_trig:
	hcsr04_free_trig(drv);
error_cleanup_echo:
	hcsr04_free_echo(drv);

out:
	return err;
}

int hcsr04_unregister(struct hcsr04_drv* drv) {
	int err = 0;

	ultrasonic_unregister_device(&drv->udrv);
	hcsr04_free_trig(drv);
	hcsr04_free_echo(drv);
	return err;
}

int hcsr04_read(struct ultrasonic_drv* udrv) {
	int echo_us, last_trig_us;
	long timeout;
	struct  hcsr04_drv *dev = (struct hcsr04_drv*)udrv;

	// only one measurement at a time allowed
	// if measuement in progress, wait for its results
	if (down_trylock(&drv.sem)) {
		if (down_interruptible(&drv.sem) == 0) {
			echo_us = dev->ping_us;
			up(&drv.sem);

			return hcsr04_cm_from_us(echo_us);
		}
		// someone interrupted us - return last known value
		return hcsr04_cm_from_us(dev->ping_us);
	}

	last_trig_us = hcsr04_us_from_trigger(dev);

	if (hcsr04_is_us_valid(dev->ping_us)) {
		// ping was rxed, so no need to wait till it fades out
		if (last_trig_us < HSCR04_ECHO_FADE_MIN_US) {
			udelay(HSCR04_ECHO_FADE_MIN_US - last_trig_us);
		}
	} else {
		// no ping rxed previously. let's wait for full echo fade period
		if (last_trig_us < HSCR04_ECHO_FADE_US) {
			udelay(HSCR04_ECHO_FADE_US - last_trig_us);
		}
	}

	hcsr04_set_echo_edge(dev, 1);
	dev->ping_trigger = 1;
	dev->ping_rxed = 0;

	// trigger ping signal
	enable_irq(dev->irq_echo);
	gpio_set_value(dev->gpio_trig, hcsr04_get_level_trigger(dev, 1));
	udelay(HSCR04_TRIGGER_US);
	gpio_set_value(dev->gpio_trig, hcsr04_get_level_trigger(dev, 0));

	// wait for echo reception
	timeout = wait_event_timeout(dev->wq_echo, dev->ping_rxed, msecs_to_jiffies(HSCR04_MEASUREMENT_TIMEOUT_MS) + 1);
	disable_irq(dev->irq_echo);

	if (!dev->ping_rxed || !timeout) {
		dev->ping_us = HSCR04_NO_OBSTACLE_US;
	}

	echo_us = dev->ping_us;

	// measurement done
	up(&drv.sem);

	return hcsr04_cm_from_us(echo_us);
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void) {
	drv.gpio_echo = gpio_echo;
	drv.gpio_trig = gpio_trig;
	drv.inverse_echo = inverse_echo;
	drv.inverse_trig = inverse_trigger;
	drv.ping_us = HSCR04_NO_OBSTACLE_US;
	init_waitqueue_head(&drv.wq_echo);
	sema_init(&drv.sem, 1);

	return hcsr04_register(&drv);
}

/* Driver Exit */
void __exit
ultrasonic_cleanup(void) {
	hcsr04_unregister(&drv);
	return;
}

module_init(ultrasonic_init)
module_exit(ultrasonic_cleanup)

MODULE_AUTHOR("Michal Stawinski <michal.stawinski@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("2012-07-26");

