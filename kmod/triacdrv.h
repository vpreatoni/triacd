#ifndef TRIACDRV_H
#define TRIACDRV_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/moduleparam.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Preatoni");
MODULE_DESCRIPTION("OpenIndoor Opto-TRIAC phase driver");
MODULE_VERSION("0.1");


/* Sets name, GPIO pin and positive and negative phase conduction angles
 * eg: insmod triac1drv.ko name=mytriac gpio=6 pos=40 neg=30
 */
static char *name = "TRIAC1";
module_param(name, charp, 0);
MODULE_PARM_DESC(name, "Sets GPIO friendly name. \"TRIAC1\" by default.");

static unsigned int gpio = 26;
module_param(gpio, uint, 0);
MODULE_PARM_DESC(gpio, "Sets ARM GPIO output pin connected to TRIAC. GPIO26 by default.");

static unsigned int pos = 0;
module_param(pos, uint, 0);
MODULE_PARM_DESC(pos, "Sets TRIAC positive cycle conduction angle. MIN=0 MAX=180 degrees.");

static unsigned int neg = 0;
module_param(neg, uint, 0);
MODULE_PARM_DESC(neg, "Sets TRIAC negative cycle conduction angle. MIN=0 MAX=180 degrees.");

/* External functions exported from aclinedrv.ko */
extern unsigned int acline_get_period(void);
extern unsigned int acline_get_optohyst(void);
extern ktime_t acline_get_sync_timestamp(void);
extern unsigned int acline_get_irq(void);
extern struct kobject * acline_get_kobject(void);


/* Time conversion constants */
#define USEC_TO_NANOSEC			1000U
#define MSEC_TO_NANOSEC			(1000U * USEC_TO_NANOSEC)
#define SEC_TO_NANOSEC			(1000U * MSEC_TO_NANOSEC)

/* TRIAC pulse definitions */
#define HIGH_CONDUCTION_ANGLE	1500U * USEC_TO_NANOSEC
#define TRIAC_TRIGGER_PULSE		10U //us
#define TRIAC_LONG_PULSE		500U //us

/* Declared atomic to avoid mutexes */
struct triac_phase_atomic {
	atomic_t pos; /* Positive phase conduction */
	atomic_t neg; /* Negative phase conduction */
} phase;

static struct kobject *triacdrv_kobject;


/* TRIAC IRQ functions */
static void triacdrv_trigger_pulse(unsigned int phase_ns);
static unsigned int triacdrv_phase_to_ns(unsigned int phase, unsigned int period_ns);
static int triacdrv_irq_start(void);
static void triacdrv_irq_end(void);
static irq_handler_t triacdrv_gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t triacdrv_gpio_irq_handler_thread(unsigned int irq, void *dev_id, struct pt_regs *regs);


/* SYSFS functions */
static int triacdrv_sysfs_start(void);
static void triacdrv_sysfs_end(void);
static ssize_t triacdrv_set(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count);
static ssize_t triacdrv_get(struct kobject *kobj, struct kobj_attribute *attr, char *buff);

static struct kobj_attribute sysfs = __ATTR(triac0, 0664, triacdrv_get, triacdrv_set);


/* INIT functions */
static int triacdrv_gpio_start(void);
static void triacdrv_gpio_end(void);

static int __init triacdrv_init(void);
static void __exit triacdrv_exit(void);

#endif // TRIACDRV_H
