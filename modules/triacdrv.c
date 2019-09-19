/*
 * TODO: state machine must be moved here, so a single GPIO driver is used (on, off, sym, asym)
 * triacdrv.c - triggers a single TRIAC according to requested conduction
 * angle.
 * 
 * 
 * This module will trigger a single TRIAC on its configured GPIO pin.
 * It will make calculations to convert requested phase conduction angle
 * to a nanosecond-precision timestamp for doing the trigger.
 * 
 * Module continuously request period measurements to adjust for small
 * deviations in AC mains signal. It also compensates for optocoupler
 * hysteresis time.
 * It adjusts trigger pulse width for high or low conduction angles to
 * prevent TRIAC misfire.
 * 
 * Phase control is asymmetrical, so a non-zero mean-value signal can be
 * generated. Useful for obtaining a positive or negative DC value
 * for motor control or Peltier cells.
 * 
 * It also provides a sysfs interface for runtime changing phase angles
 *
 * Copyright (C) 2019 Victor Preatoni
 */

#include "triacdrv.h"


/* SYSFS section to allow reading and writing phase
 * conduction angles from user-mode
 */
static int triacdrv_sysfs_start(void)
{
	/* Module requires aclinedrv.ko to be running */
	triacdrv_kobject = acline_get_kobject();
	
	sysfs.attr.name = name;

	if (triacdrv_kobject) {
		if (sysfs_create_file(triacdrv_kobject, &sysfs.attr)) {
			printk(KERN_ERR "%s: failed to create sysfs\n", name);
			return -EIO;
		}
		else {
			printk(KERN_INFO "%s: GPIO %02u - /sys/%s/%s\n", name, gpio, triacdrv_kobject->name, name);
			return 0;
		}
	}
	else
		return -EIO;
}

static void triacdrv_sysfs_end(void)
{
	sysfs_remove_file(triacdrv_kobject, &sysfs.attr);
	return;
}

/* Writer function for phase angles. If only one parameter is received,
 * it assumes a symmetrical phase. If two parameters received, asymmetrical.
 */
static ssize_t triacdrv_set(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count)
{
	unsigned int pos_phase;
	unsigned int neg_phase;
	unsigned int phase_vars;
	
	phase_vars = sscanf(buff, "%u %u", &pos_phase, &neg_phase);
	switch (phase_vars) {
	case 2:
		if (pos_phase > 180 || neg_phase > 180)
			printk(KERN_ERR "%s: phase limit is 0-180 degrees\n", name);
		else {
			atomic_set(&phase.pos, pos_phase);
			atomic_set(&phase.neg, neg_phase);
		}
		break;
		
	case 1:
		if (pos_phase > 180)
			printk(KERN_ERR "%s: phase limit is 0-180 degrees\n", name);
		else {
			atomic_set(&phase.pos, pos_phase);
			atomic_set(&phase.neg, pos_phase);
		}
		break;
		
	default:
		printk(KERN_ERR "%s: wrong parameter\n", name);
	}
	
	return count;
}

/* Returns current phase for positive and negative conduction angles */
static ssize_t triacdrv_get(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	unsigned int pos_phase;
	unsigned int neg_phase;
	int count;
	
	pos_phase = atomic_read(&phase.pos);
	neg_phase = atomic_read(&phase.neg);

	count = scnprintf(buff, PAGE_SIZE, "%u %u\n", pos_phase, neg_phase);

	return count;
}

/* Will convert angle to nanoseconds
 * In case phase is zero, will returno zero and not period_ns / 2
 * as expected.
 * This allows pulse skipping
 */
static unsigned int triacdrv_phase_to_ns(unsigned int phase, unsigned int period_ns)
{
	return (phase ? ((180 - phase) * period_ns / 360) : 0);
}

/* Properly triggers a single TRIAC */
static void triacdrv_trigger_pulse(unsigned int phase_ns)
{
	gpio_set_value(gpio, 1);
	if (phase_ns < HIGH_CONDUCTION_ANGLE)
		/* When conduction angle is high, TRIAC needs longer trigger */
		usleep_range(TRIAC_LONG_PULSE, TRIAC_LONG_PULSE * 2);
	else
		/* Short trigger */
		udelay(TRIAC_TRIGGER_PULSE);
	gpio_set_value(gpio, 0);

	return;
}

/* IRQ section */
static int triacdrv_irq_start(void)
{   
	if (request_threaded_irq(acline_get_irq(), (irq_handler_t)triacdrv_gpio_irq_handler, (irq_handler_t)triacdrv_gpio_irq_handler_thread, IRQF_TRIGGER_RISING | IRQF_SHARED, name, (void *)(triacdrv_gpio_irq_handler))) {
		printk(KERN_ERR "IRQ %d: could not request\n", acline_get_irq());
		return -EIO;
	}
	else 
		return 0;
}
	
static void triacdrv_irq_end(void)
{
	free_irq(acline_get_irq(), (void *)(triacdrv_gpio_irq_handler));
	return;
}

/* Simple approach to threaded IRQs. Since TRIAC trigger pulse will occur a few
 * microseconds later, we cannot sleep on main IRQ.
 */
static irq_handler_t triacdrv_gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	if (dev_id == (void *)(triacdrv_gpio_irq_handler))
		return (irq_handler_t)IRQ_WAKE_THREAD;
	else
		return (irq_handler_t)IRQ_NONE;
}

/* It is safe now to call sleep functions (like schedule_hrtimeout()) because
 * we are running on a separate thread.
 * So, after quickly making time calculations, we set the schedule_hrtimeout()
 * to the requested one. After schedule_hrtimeout() returns, we have to
 * immediately trigger the TRIAC, as we are on the exact trigger time for the
 * requested phase conduction angle.
 * Hopefully, threaded IRQs run on a realtime priority, so no other task
 * should preempt us. If that happens, it would be catastrophical for TRIAC
 * phase control!.
 */
static irq_handler_t triacdrv_gpio_irq_handler_thread(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	ktime_t irq_timestamp;
	ktime_t pos_trigger_timestamp;
	ktime_t neg_trigger_timestamp;
	unsigned int period_ns;
	unsigned int pos_phase_ns;
	unsigned int neg_phase_ns;
	unsigned int pos_phase = atomic_read(&phase.pos);
	unsigned int neg_phase = atomic_read(&phase.neg);
	
	/* If both phases are zero, turn off triac */
	if (!pos_phase && !neg_phase) {
		gpio_set_value(gpio, 0);
		return (irq_handler_t)IRQ_HANDLED;
	}
	
	/* If both phases are 180, fully turn on triac */
	if (pos_phase == 180 && neg_phase == 180) {
		gpio_set_value(gpio, 1);
		return (irq_handler_t)IRQ_HANDLED;
	}
	
	period_ns = acline_get_period();
	pos_phase_ns = triacdrv_phase_to_ns(pos_phase, period_ns);
	neg_phase_ns = triacdrv_phase_to_ns(neg_phase, period_ns);
	
	irq_timestamp = ktime_add_ns(acline_get_sync_timestamp(), acline_get_optohyst());
	
	if (neg_phase_ns) {
		/* Begin TRIAC trigger on negative cycle */
		neg_trigger_timestamp = ktime_add_ns(irq_timestamp, neg_phase_ns);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&neg_trigger_timestamp, HRTIMER_MODE_ABS);
		triacdrv_trigger_pulse(neg_phase_ns);
	}
	
	if (pos_phase_ns) {
		/* Begin TRIAC trigger on positive cycle */
		pos_trigger_timestamp = ktime_add_ns(irq_timestamp, (pos_phase_ns + period_ns / 2));
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&pos_trigger_timestamp, HRTIMER_MODE_ABS);
		triacdrv_trigger_pulse(pos_phase_ns);
	}

	return (irq_handler_t)IRQ_HANDLED;
}



/* GPIO config section
 */
static int triacdrv_gpio_start(void)
{
	if (gpio_request_one(gpio, GPIOF_OUT_INIT_LOW, name)) {
		printk(KERN_ERR "%s: GPIO error\n", name);
		return -EIO;
	}
	else {
		atomic_set(&phase.pos, pos);
		atomic_set(&phase.neg, neg);
		return 0;
	}
}

static void triacdrv_gpio_end(void)
{
	gpio_free(gpio);

	return;
}


/* MODULE init and exit routines
 */
static int __init triacdrv_init(void)
{
	int err;
	
	err = triacdrv_gpio_start();
	if (err)
		goto fail_gpio; //Critical fail
	
	err = triacdrv_sysfs_start();
	if (err)
		goto fail_sysfs; //Critical fail
		
	err = triacdrv_irq_start();
	if (err)
		goto fail_irq;
		
	printk(KERN_INFO "%s: ready\n", name);
	return 0;
	
	fail_irq:		triacdrv_sysfs_end();
	fail_sysfs:		triacdrv_gpio_end();
	fail_gpio:		printk(KERN_ERR "%s: failed to initialize\n", name);
					return err;
}

static void __exit triacdrv_exit(void)
{
	triacdrv_irq_end();
	triacdrv_sysfs_end();
	triacdrv_gpio_end();
	
	printk(KERN_INFO "%s: stopped\n", name);
	
	return;
}

module_init(triacdrv_init);
module_exit(triacdrv_exit);
