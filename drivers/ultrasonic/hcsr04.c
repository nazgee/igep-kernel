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
#include <linux/interrupt.h>

#define HARDWARE_NAME	"hcsr04"
#define HSCR04_TRIG_LEVEL 1
#define HSCR04_ECHO_LEVEL 1

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
	struct timeval time_trigger;
	struct timeval time_echo;
	int foo;
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

int hcsr04_read(struct ultrasonic_drv* udrv) {
	struct  hcsr04_drv *dev = (struct hcsr04_drv*)udrv;
	do_gettimeofday(&dev->time_trigger);
	return dev->foo;
//	return 666;
}

static int hcsr04_get_level_trigger(struct hcsr04_drv* drv, int is_triggered) {
	return is_triggered ^ drv->inverse_trig;
}

static int hcsr04_get_level_echo(struct hcsr04_drv* drv, int is_echoed) {
	return is_echoed ^ drv->inverse_echo;
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
		goto out;
	}
out:
	return err;
}

static irqreturn_t hcsr04_echo_isr(int irq, void *cookie)
{
	struct  hcsr04_drv *dev = (struct hcsr04_drv*)cookie;
	do_gettimeofday(&dev->time_echo);
	dev->foo++;

	printk(KERN_ERR "aaa! irq!\n");
	return IRQ_HANDLED;
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

	err = request_irq(drv->irq_echo, hcsr04_echo_isr, IRQF_SHARED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, HARDWARE_NAME".echo", drv);
	if (err < 0) {
		printk(KERN_ERR "request_irq() failed: %d\n", err);
		goto error_cleanup_irq; // to nie jest potrzebne FIXME
	}
//	set_irq_type(drv->irq_echo, IRQ_TYPE_EDGE_BOTH);
	// disable_irq(drv->irq_echo);

	goto out;

error_cleanup_irq:
	free_irq(drv->irq_echo, drv);
error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

static int hcsr04_request_trig(struct hcsr04_drv* drv) {
	int err = 0;
	int gpio = drv->gpio_trig;

	err = hcsr04_request_gpio(gpio, drv->id, "trig");
	if (err) {
		goto out;
	}

	err = gpio_direction_output(gpio, hcsr04_get_level_trigger(drv, 0));
	if (err) {
		printk(KERN_ERR "could not set direction of gpio%d to output!\n", gpio);
		goto error_cleanup_request;
	}

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
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

error_cleanup_trig:
	gpio_free(drv->gpio_trig);
error_cleanup_echo:
	gpio_free(drv->gpio_echo);
out:
	return err;
}

int hcsr04_unregister(struct hcsr04_drv* drv) {
	int err = 0;

	ultrasonic_unregister_device(&drv->udrv);
	free_irq(drv->irq_echo, drv);
	gpio_free(drv->gpio_trig);
	gpio_free(drv->gpio_echo);
	return err;
}

/*
 * Driver Initialization
 */
int __init ultrasonic_init(void) {
	drv.gpio_echo = gpio_echo;
	drv.gpio_trig = gpio_trig;
	drv.inverse_echo = inverse_echo;
	drv.inverse_trig = inverse_trigger;

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

