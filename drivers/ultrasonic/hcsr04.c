/*
 * ultrasonic.c
 *
 *  Created on: Jul 26, 2012
 *      Author: nazgee
 */

#include <linux/ultrasonic/ultrasonic.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>

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
	int gpio_trig;
	int gpio_echo;
	int inverse_trig;
	int inverse_echo;
};

static struct hcsr04_drv drv = {
	.udrv = {
		.measure = hcsr04_read
	},
	.gpio_trig = -EINVAL,
	.gpio_echo = -EINVAL,
	.inverse_trig = 0,
	.inverse_echo = 0
};

int hcsr04_read(struct ultrasonic_drv* udrv) {
	return 666;
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
		printk(KERN_ERR "gpio_request failed for %s @ %d\n", label, gpio);
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

static int hcsr04_request_gpio_echo(struct hcsr04_drv* drv) {
	int err = 0;
	int gpio = drv->gpio_trig;

	err = hcsr04_request_gpio(gpio, drv->id, "echo");
	if (err) {
		goto out;
	}

	err = gpio_direction_input(gpio);
	if (err) {
		goto error_cleanup_request;
	}

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

static int hcsr04_request_gpio_trig(struct hcsr04_drv* drv) {
	int err = 0;
	int gpio = drv->gpio_trig;

	err = hcsr04_request_gpio(gpio, drv->id, "trig");
	if (err) {
		goto out;
	}

	err = gpio_direction_output(gpio, hcsr04_get_level_trigger(drv, 0));
	if (err) {
		goto error_cleanup_request;
	}

error_cleanup_request:
	gpio_free(gpio);
out:
	return err;
}

int hcsr04_register(struct hcsr04_drv* drv) {
	int err;

	err = hcsr04_request_gpio_echo(drv);
	if (err) {
		goto out;
	}

	err = hcsr04_request_gpio_trig(drv);
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
	int err;

	ultrasonic_unregister_device(&drv->udrv);
	gpio_free(drv->gpio_trig);
	gpio_free(drv->gpio_echo);
	out:
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

