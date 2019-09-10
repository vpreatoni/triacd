/*
 * aclinedrv.c - measures AC line period from GPIO pin connected to an
 * optocoupler driven by AC mains.
 * 
 * It also does some corrections due to optocoupler assymetry.
 * 
 * This module serves as a trigger point to triacdrv.ko module, which
 * needs to know when the AC line zero-crosses to perform advanced TRIAC
 * phase control mechanisms.
 * 
 * Module continuously measures mains period with nanosecond precision
 * for a precise phase control of TRIACs.
 * 
 * It provides frequency measurement on a sysfs export
 *
 * Copyright (C) 2019 Victor Preatoni
 */

#include "aclinedrv.h"


/* EXPORTS section to allow reading
 * critical data from another Kernel module
 */

/* timestamp for AC mains zero crossing */
ktime_t acline_get_sync_timestamp(void)
{
	ktime_t local_timestamp;
	
	spin_lock_irqsave(&acline_phase.lock, acline_phase.spin_flags);
	local_timestamp = acline_phase.timestamp;
	spin_unlock_irqrestore(&acline_phase.lock, acline_phase.spin_flags);
	
	return local_timestamp;
}
EXPORT_SYMBOL(acline_get_sync_timestamp);


/* period (in ns) of AC mains
 * If period is out of bounds, returns 0
 */
unsigned int acline_get_period(void)
{
	ktime_t local_period_time;
	unsigned int period_ns;
	
	spin_lock_irqsave(&acline_phase.lock, acline_phase.spin_flags);
	local_period_time = acline_phase.period_time;
	spin_unlock_irqrestore(&acline_phase.lock, acline_phase.spin_flags);
	
	period_ns = (unsigned int)ktime_to_ns(local_period_time);
	/* Limit calculation to normal mains Hz boundary */
	if (period_ns > MIN_PERIOD_ns && period_ns < MAX_PERIOD_ns)
		return period_ns;
	else
		return 0;
}
EXPORT_SYMBOL(acline_get_period);


/* Returns opto_hysteresis time so TRIACs can compensate
 * trigger time
 */
unsigned int acline_get_optohyst(void)
{
	return calibration.opto_hysteresis;
}
EXPORT_SYMBOL(acline_get_optohyst);

/* Returns irq number used to syncronize
 * TRIACs on zero-crossing phase
 */
unsigned int acline_get_irq(void)
{
	return irqNumber;
}
EXPORT_SYMBOL(acline_get_irq);


/* Returns sysfs kobject to create new nodes
 * on the same triacd directory
 */
struct kobject * acline_get_kobject(void)
{
	return acline_kobject;
}
EXPORT_SYMBOL(acline_get_kobject);



/* SYSFS section to allow reading AC mains
 * frequency from user-mode
 */
static int acline_sysfs_start(void)
{
	acline_kobject = kobject_create_and_add(SYSFS_NODE, NULL);

	if (acline_kobject) {
		if (sysfs_create_file(acline_kobject, &sysfs.attr)) {
			printk(KERN_ERR "AC LINE: failed to create sysfs\n");
			return -EIO;
		}
		else {
			printk(KERN_INFO "AC LINE: frequency on /sys/%s/%s\n", acline_kobject->name, sysfs.attr.name);
			return 0;
		}
	}
	else
		return -EIO;
}

static void acline_sysfs_end(void)
{
	kobject_put(acline_kobject);
	return;
}

/* Reader function for frequency. Fixed point arithmetics (2 decimal digits) */
static ssize_t acline_get_freq(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	int count;
	/* Frequency is stored times 100 to allow fixed point arithmetics */
	unsigned int freqx100, freq, freqdecimals;
	unsigned int period_ns;
	ktime_t local_period_time;
	
	spin_lock_irqsave(&acline_phase.lock, acline_phase.spin_flags);
	local_period_time = acline_phase.period_time;
	spin_unlock_irqrestore(&acline_phase.lock, acline_phase.spin_flags);
	
	period_ns = (unsigned int)ktime_to_ns(local_period_time);
	/* Limit calculation to normal mains Hz boundary */
	if (period_ns > MIN_PERIOD_ns && period_ns < MAX_PERIOD_ns)
		freqx100 = SEC_TO_NANOSEC / (period_ns / 100);
	else
		freqx100 = 0;
	
	if (freqx100) {
		freq = freqx100 / 100;
		freqdecimals = freqx100 - freq * 100;
		count = scnprintf(buff, PAGE_SIZE, "%02u.%02uHz\n", freq, freqdecimals);
	}
	else
		count = scnprintf(buff, PAGE_SIZE, "error\n");
	
	return count;
}



/* IRQ handlers section. Due to the high precision needed for period
 * calculations, AC mains signal must be processed by interrupt routines
 */

static u64 int_pow(u64 base, unsigned int exp)
{
	u64 result = 1;
	
	while (exp) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}
	
	return result;
}

/* AC mains optocoupler requires the use of high value series resistors to avoid
 * burning it's LED due to high voltage (311V peak on 220V RMS). This high
 * resistor value causes LED to prematurely turn off before AC mains actually
 * reaches it's zero-crossing point. This effect causes an assymetry on the
 * positive and negative cycles of the "squared" AC mains signal. It can be
 * easyly calibrated averaging positive cycles time and negative cycles time,
 * and then substracting each other and dividing value by 4.
 * This function launches and IRQ handler to measure rising and falling edge
 * times, waits for CALIB_TIME_MS time, disables interrupt, and makes
 * calculations.
 */
static int acline_irq_calibrate(void)
{
	unsigned int i;
	unsigned int accum;
	unsigned int avg_pos, avg_neg;
	unsigned int std_pos, std_neg;
	
	irqNumber = gpio_to_irq(opto_input);
	calibration.samples_pos = 0;
	calibration.samples_neg = 0;
	acline_phase.timestamp = 0;
	
	if (request_irq(irqNumber, (irq_handler_t)acline_calibration_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "calibrateAC", (void *)(acline_calibration_irq_handler)))
		printk(KERN_ERR "IRQ %d: could not request\n", irqNumber);
	else {
		msleep(CALIB_TIME_MS);
		free_irq(irqNumber, (void *)(acline_calibration_irq_handler));
		
		/* Only process data if we have some measurements */
		if (calibration.samples_pos && calibration.samples_neg) {
			for (i = 0, accum = 0; i < calibration.samples_pos; i++)
				accum += ktime_to_ns(calibration.period_pos[i]);
			avg_pos = accum / i;
			
			for (i = 0, accum = 0; i < calibration.samples_pos; i++)
				accum += int_pow((ktime_to_ns(calibration.period_pos[i]) - avg_pos), 2);
			std_pos = int_sqrt(accum / i);
			
			for (i = 0, accum = 0; i < calibration.samples_neg; i++)
				accum += ktime_to_ns(calibration.period_neg[i]);
			avg_neg = accum / i;
			
			for (i = 0, accum = 0; i < calibration.samples_neg; i++)
				accum += int_pow((ktime_to_ns(calibration.period_neg[i]) - avg_neg), 2);
			std_neg = int_sqrt(accum / i);
			
			/* If standard deviation is below some reasonable value (50us)
			 * we can now calculate the optocoupler hysteresis
			 */
			if (std_neg < (50 * USEC_TO_NANOSEC) && std_pos < (50 * USEC_TO_NANOSEC)) {
				calibration.opto_hysteresis = (avg_neg - avg_pos) / 4;
				return 0; /* IRQ calibrated */
			}
		}
	}
	
	return -1;
}

/* Calibration IRQ handler. Will only run for a few secs and then
 * IRQ is released
 */
static irq_handler_t acline_calibration_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	/* First run */
	if (!acline_phase.timestamp)
		acline_phase.timestamp = ktime_get();
	else {
		/* Avoid overflowing buffer if it is full */
		if (calibration.samples_pos < CALIB_BUFFER_LENGTH && calibration.samples_neg < CALIB_BUFFER_LENGTH) {
			if (gpio_get_value(opto_input)) {
				acline_phase.old_timestamp = acline_phase.timestamp;
				acline_phase.timestamp = ktime_get();
				calibration.period_pos[calibration.samples_pos] = ktime_to_ns(ktime_sub(acline_phase.timestamp, acline_phase.old_timestamp));
				calibration.samples_pos ++;
			}
			else {
				acline_phase.old_timestamp = acline_phase.timestamp;
				acline_phase.timestamp = ktime_get();
				calibration.period_neg[calibration.samples_neg] = ktime_to_ns(ktime_sub(acline_phase.timestamp, acline_phase.old_timestamp));
				calibration.samples_neg ++;
			}
		}
	}
	
	return (irq_handler_t)IRQ_HANDLED;
}

/* Standard IRQ routine that will install handler on RISING edge only.
 * The RISING edge trigger occurs BEFORE AC mains reaches zero, so
 * it is very usefull, as we have calibration.opto_hysteresis nanoseconds
 * of grace time to perform some complex calculations before we start doing
 * something else (like triggering TRIACs)
 */
static int acline_irq_start(void)
{   
	irqNumber = gpio_to_irq(opto_input);
	
	if (request_irq(irqNumber, (irq_handler_t)acline_gpio_irq_handler, IRQF_TRIGGER_RISING | IRQF_SHARED, "lineAC", (void *)(acline_gpio_irq_handler))) {
		printk(KERN_ERR "IRQ %d: could not request\n", irqNumber);
		return -EIO;
	}
	else {
		spin_lock_init(&acline_phase.lock);
		return 0;
	}
}
	
static void acline_irq_end(void)
{
	free_irq(irqNumber, (void *)(acline_gpio_irq_handler));
	return;
}

/* Very simple IRQ handler that will precisely calculate period time */
static irq_handler_t acline_gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
	if (dev_id == (void *)(acline_gpio_irq_handler)) {
		acline_phase.old_timestamp = acline_phase.timestamp;
		acline_phase.timestamp = ktime_get();
		acline_phase.period_time = ktime_sub(acline_phase.timestamp, acline_phase.old_timestamp);
		return (irq_handler_t)IRQ_HANDLED;
	}
	else
		return (irq_handler_t)IRQ_NONE;
}



/* GPIO config section
 */
static int acline_gpio_start(void)
{
	return (gpio_request_one(opto_input, GPIOF_IN, "ACLINE"));
}

static void acline_gpio_end(void)
{
	gpio_free(opto_input);
	
	return;
}



/* MODULE init and exit routines
 */
static int __init acline_init(void)
{
	int err;
	
	err = acline_gpio_start();
	if (err)
		goto fail_gpio;
	
	err = acline_sysfs_start();
	if (err)
		goto fail_sysfs;
		
	printk(KERN_INFO "AC LINE: calibrating...\n");
	if (acline_irq_calibrate()) {
		printk(KERN_ERR "AC LINE: unstable frequency\n");
		calibration.opto_hysteresis = DEFAULT_OPTO_HYSTERESIS;
	}
	else
		printk(KERN_INFO "AC LINE: optocoupler hysteresis = %uus\n", calibration.opto_hysteresis / 1000);
		
	err = acline_irq_start();
	if (err)
		goto fail_irq;
	
	printk(KERN_INFO "AC LINE: ready\n");
	return 0;
	
	
	fail_irq:		acline_sysfs_end();
	fail_sysfs:		acline_gpio_end();
	fail_gpio:		printk(KERN_ERR "AC LINE: failed to initialize\n");
					return err;
}

static void __exit acline_exit(void)
{
	acline_irq_end();
	acline_sysfs_end();
	acline_gpio_end();
	
	printk(KERN_INFO "AC LINE: stopped\n");
	
	return;
}

module_init(acline_init);
module_exit(acline_exit);
