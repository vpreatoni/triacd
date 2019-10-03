#ifndef ACLINEDRV_H
#define ACLINEDRV_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Preatoni");
MODULE_DESCRIPTION("OpenIndoor Optocoupler phase feedback driver");
MODULE_VERSION("0.1");

static const struct of_device_id triac_of_match[] = {
	{ .compatible = "triacboard,quadtriac,dualtriac", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, triac_of_match);

/* GPIO5 is the default input on OpenIndoor Opto-TRIAC board
 * It can be easily changed passing argument to insmod:
 * eg: insmod aclinedrv.ko opto_input=20
 */
static unsigned int opto_input = 5;
module_param(opto_input, uint, 0);
MODULE_PARM_DESC(opto_input, "Sets ARM GPIO pin number used to read phase feedback input. GPIO5 by default.");

/* Time conversion constants */
#define SEC_TO_MSEC				1000U
#define USEC_TO_NANOSEC			1000U
#define MSEC_TO_NANOSEC			(1000U * USEC_TO_NANOSEC)
#define SEC_TO_NANOSEC			(1000U * MSEC_TO_NANOSEC)

/* sysfs entry node */
#define SYSFS_NODE  "triacd"
#define SYSFS_OBJECT  freq

/* Minimum accepted frequency */
#define MIN_FREQUENCY			40U //Hz
/* Maximum accepted frequency */
#define MAX_FREQUENCY			70U //Hz
/* Minimum accepted period */
#define MIN_PERIOD_ns			(SEC_TO_NANOSEC / MAX_FREQUENCY)
/* Maximum accepted period */
#define MAX_PERIOD_ns			(SEC_TO_NANOSEC / MIN_FREQUENCY)

/* Default value in case automatic calibration fails */
#define DEFAULT_OPTO_HYSTERESIS	(320U * USEC_TO_NANOSEC)
/* Time to perform averaging */
#define CALIB_TIME_MS			5000
/* Buffer lenght for averaging period time */
#define CALIB_BUFFER_LENGTH		((CALIB_TIME_MS / SEC_TO_MSEC) * MAX_FREQUENCY)


static unsigned int irqNumber;

/* AC mains time measurements struct */
static struct acline_time {
	ktime_t timestamp;
	ktime_t old_timestamp;
	ktime_t period_time;
	spinlock_t lock;
	unsigned long spin_flags;
} acline_phase;

/* Optocoupler calibration struct */
static struct calib {
	ktime_t period_pos[CALIB_BUFFER_LENGTH];
	ktime_t period_neg[CALIB_BUFFER_LENGTH];
	unsigned int samples_pos;
	unsigned int samples_neg;
	unsigned int opto_hysteresis;
} calibration;

static struct kobject *acline_kobject;

/* Exported functions */
ktime_t acline_get_sync_timestamp(void);
unsigned int acline_get_period(void);
unsigned int acline_get_optohyst(void);
unsigned int acline_get_irq(void);
struct kobject * acline_get_kobject(void);

/* IRQ functions */
static u64 int_pow(u64 base, unsigned int exp);
static int acline_irq_start(void);
static void acline_irq_end(void);
static irq_handler_t acline_gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
// static irq_handler_t acline_gpio_irq_handler_thread(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t acline_calibration_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static int acline_irq_calibrate(void);

/* GPIO functions */
static int acline_gpio_start(void);
static void acline_gpio_end(void);

/* SYSFS functions */
static int acline_sysfs_start(void);
static void acline_sysfs_end(void);
static ssize_t acline_get_freq(struct kobject *kobj, struct kobj_attribute *attr, char *buff);
static struct kobj_attribute sysfs = __ATTR(SYSFS_OBJECT, 0444, acline_get_freq, NULL);


static int __init acline_init(void);
static void __exit acline_exit(void);

#endif // ACLINEDRV_H
