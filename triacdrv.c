#include "triacdrv.h"



/**************** SYSFS *****************/
static int sysfs_start(void)
{
    unsigned int i; 
    int retval = -EIO; //Start with error
    
	triac_kobject = kobject_create_and_add(SYSFS_NODE, NULL);
    
    if(triac_kobject) {
        for(i=0; i < triac_vector_len; i++) {
            if(triac[i].gpio_status != disabled && triac[i].gpio_status != error) {
                if(sysfs_create_file(triac_kobject, &triac[i].sysfs.attr)) {
                    printk(KERN_ERR " %s: failed to create sysfs\n", triac[i].label);
                    triac[i].gpio_status = error;
                }
                else {
                    printk(KERN_INFO "%s on GPIO %u: accesible on /sys/%s/%s\n", triac[i].label, triac[i].gpio, SYSFS_NODE, triac[i].sysfs.attr.name);
                    mutex_init(&triac[i].phase_lock);
                    triac[i].gpio_status = enabled;
                    retval = 0; //At least one SYSFS configured. Clear error
                }
            }
        }
    }
    
    return retval;
}

static void sysfs_end(void)
{
    unsigned int i;
    
    //If running, stop all running child threads
    for(i=0; i<(triac_vector_len - 1); i++) triac_fade_thread_end(i);
    
    kobject_put(triac_kobject);
}

static ssize_t set_triac(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count)
{
    unsigned int channel;
    unsigned int i;
    unsigned int pos_phase;
    unsigned int neg_phase;
    unsigned int phase_vars;
    char param[16];
    unsigned int fade_stop_phase;
    unsigned int fade_time;
    

    if(kstrtouint(attr->attr.name, 10, &channel)) {
        printk(KERN_ERR "Opto-TRIAC Board: error\n");
        return count;
    }
    i = channel - 1; //Channel is decremented to correspond value with the triac's vector
    
    if((i) >= (triac_vector_len - 1)) 
    {
        printk(KERN_ERR "Opto-TRIAC Board: error\n");
        return count;
    }
    
    
    phase_vars = sscanf(buff, "%s %u %u", param, &fade_stop_phase, &fade_time);
    if(phase_vars == 3) {
        if(strstr(param, "fade")) {
            if(fade_stop_phase > 180) printk(KERN_ERR "Opto-TRIAC Board: phase limit is 0-180 degrees\n");
            else {
                triac_fade_thread_end(i); //If fader is running, need to be stopped and restarted
                usleep_range(1000, 2000); //Small delay to allow old fader to fully stop
                if(triac_fade_thread_start(i, fade_stop_phase, fade_time))
                    printk(KERN_ERR "%s: can not start fader\n", triac[i].label);
            }
        }
        else printk(KERN_ERR "Opto-TRIAC Board: wrong parameter\n");
        
        return count;
    }
    
    phase_vars = sscanf(buff, "%u %u", &pos_phase, &neg_phase);
    switch(phase_vars) {
        case 2: { //Dual parameters
            if(pos_phase > 180 || neg_phase > 180) printk(KERN_ERR "Opto-TRIAC Board: phase limit is 0-180 degrees\n");
            else {
                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                triac[i].phase.pos = pos_phase;
                triac[i].phase.neg = neg_phase;
                mutex_unlock(&triac[i].phase_lock);
            }
        } break;
        
        case 1: { //Single parameter
            if(pos_phase > 180) printk(KERN_ERR "Opto-TRIAC Board: phase limit is 0-180 degrees\n");
            else {
                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                triac[i].phase.pos = pos_phase;
                triac[i].phase.neg = pos_phase;
                mutex_unlock(&triac[i].phase_lock);
            }
        } break;
        
        
        default: printk(KERN_ERR "Opto-TRIAC Board: wrong parameter\n");
    }
    
	return count;
}


static ssize_t get_triac(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
    unsigned int channel;
    unsigned int i;
    int count;
    struct triac_phase local_phase;
    
    if(kstrtouint(attr->attr.name, 10, &channel)) {
        count = scnprintf(buff, PAGE_SIZE, "error\n");
        return count;
    }
    i = channel - 1; //Channel is decremented to correspond value with the triac's vector
    
    if(i >= (triac_vector_len - 1)) {
        count = scnprintf(buff, PAGE_SIZE, "error\n");
        return count;
    }
    
    mutex_lock(&triac[i].phase_lock); //Lock mutex while reading value
    local_phase.status = triac[i].phase.status;
    local_phase.pos = triac[i].phase.pos;
    local_phase.neg = triac[i].phase.neg;
    mutex_unlock(&triac[i].phase_lock);
    switch (local_phase.status) {
        case off: count = scnprintf(buff, PAGE_SIZE, "off\n"); break;
        case on: count = scnprintf(buff, PAGE_SIZE, "on\n"); break;
        case sym: count = scnprintf(buff, PAGE_SIZE, "symmetric %udeg\n", local_phase.pos); break;
        case asym: count = scnprintf(buff, PAGE_SIZE, "asymmetric %udeg / %udeg\n", local_phase.pos, local_phase.neg); break;
        default: count = scnprintf(buff, PAGE_SIZE, "error\n");
    }

	return count;
}


static ssize_t get_acline(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
    int count, i;
    unsigned int local_freqx100;
    
    mutex_lock(&freqx100_lock); //Lock mutex while reading global variable
    local_freqx100 = freqx100;
    mutex_unlock(&freqx100_lock); //Free mutex
    
    if(local_freqx100) {
        count = scnprintf(buff, PAGE_SIZE, "%04uHz\n", local_freqx100);
        for(i=0; i<6 ; i++) { //Emulated floating point shifting freq x 100 to the right and adding .
            buff[count+1-i] = buff[count-i];
        }
        buff[count+1-i] = '.';
        count++;
    }
    else count = scnprintf(buff, PAGE_SIZE, "error\n");
    
	return count;
}




/************************ THREADS ***************************/
static int triac_fade_thread_start(unsigned int i, unsigned int fade_stop_phase, unsigned int fade_time)
{
    static char thread_param[32];
    
    scnprintf(thread_param, 32, "%u %u %u", i, fade_stop_phase, fade_time);
    
    if(IS_ERR_OR_NULL(triac[i].fade_task)) { //task pointer uninitialized, initialize it
        triac[i].fade_task = kthread_create(fade_thread, thread_param, "%s_fade", triac[i].label);
        if(IS_ERR_OR_NULL(triac[i].fade_task)) {
            printk(KERN_ERR "%s fade thread: could not run\n", triac[i].label);
            return -ENOMEM;
        }
        wake_up_process(triac[i].fade_task);
    }
    
    else if(!pid_alive(triac[i].fade_task)) { //PID invalid, recreate task
        triac[i].fade_task = kthread_create(fade_thread, thread_param, "%s_fade", triac[i].label);
        
        if(IS_ERR_OR_NULL(triac[i].fade_task)) {
            printk(KERN_ERR "%s fade thread: could not run\n", triac[i].label);
            return -ENOMEM;
        }
        wake_up_process(triac[i].fade_task);
    }
    return 0;
}

static void triac_fade_thread_end(unsigned int i)
{
    /*Protected way of stopping threads. Not NULL pointer, pid_alive*/
    if(!IS_ERR_OR_NULL(triac[i].fade_task)) {
        if(pid_alive(triac[i].fade_task)) {
            wake_up_process(triac[i].fade_task); //Wake up thread if it was sleeping
            kthread_stop(triac[i].fade_task); //Send STOP signal
        }
    }
}

int fade_thread(void *data)
{
    unsigned int fade_stop_phase;
    unsigned int fade_time;
    unsigned int i;
    unsigned int delay_step_ms;
    struct triac_phase local_phase;
    
    int len = strlen(data)+1;
    
    char *mydata = kmalloc(len, GFP_KERNEL);
    memset(mydata, '\0', len);
    strncpy(mydata, data, len-1);
    sscanf(mydata, "%u %u %u", &i, &fade_stop_phase, &fade_time);
    
    if(i >= triac_vector_len - 1) { //Check for overflow, excludes ACLINE
        printk(KERN_ERR "%s fade thread: pointer overflow\n", triac[i].label);
        return -1;
    }
    
    printk(KERN_INFO "%s fade thread: started\n", triac[i].label);
    
    mutex_lock(&triac[i].phase_lock); //Lock mutex while reading value
    local_phase.pos = triac[i].phase.pos;
    local_phase.neg = triac[i].phase.neg;
    mutex_unlock(&triac[i].phase_lock);
    
    //ToDo: CONTEMPLAR FADE ASYMETRICO, bajando ambos, empezando x el mas alto.
    //Limitar delay_step a 100ms, haciendo algo adaptativo
    
    if((fade_stop_phase > local_phase.pos)) { //Fade in
        delay_step_ms = fade_time / (fade_stop_phase - local_phase.pos);
        for(; (local_phase.pos <= fade_stop_phase) && !kthread_should_stop(); local_phase.pos++ ) {
            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
            triac[i].phase.pos = local_phase.pos;
            triac[i].phase.neg = local_phase.pos;
            mutex_unlock(&triac[i].phase_lock);
            usleep_range(delay_step_ms*900, delay_step_ms*1100);
        }
    }
    else if((fade_stop_phase < local_phase.pos)) { //Fade out
        delay_step_ms = fade_time / (local_phase.pos - fade_stop_phase);
        for(; (local_phase.pos-- > fade_stop_phase) && !kthread_should_stop(); ) {
            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
            triac[i].phase.pos = local_phase.pos;
            triac[i].phase.neg = local_phase.pos;
            mutex_unlock(&triac[i].phase_lock);
            usleep_range(delay_step_ms*900, delay_step_ms*1100);
        }
    }
    
    printk(KERN_INFO "%s fade thread: stopped\n", triac[i].label);
    kfree(mydata);
    return 0;
}

static int update_thread_start(void)
{
    update_task = kthread_create(update_thread, NULL, THREAD_NAME);
    
    if(IS_ERR_OR_NULL(update_task)) {
        printk(KERN_ERR "%s: could not run\n", THREAD_NAME);
        return -ENOMEM;
    }
    
    struct sched_param thread_param = {.sched_priority = MAX_RT_PRIO - 50};
    sched_setscheduler(update_task, SCHED_FIFO, &thread_param);
    wake_up_process(update_task);
    
    return 0;
}

static void update_thread_end(void)
{
    /*Protected way of stopping threads. Not NULL pointer, pid_alive*/
    if(!IS_ERR_OR_NULL(update_task)) {
        if(pid_alive(update_task)) {
            //wake_up_process(update_task); //Wake up thread if it was sleeping
            printk(KERN_INFO "about to stop update_thread\n");
            kthread_stop(update_task);
        }
    }
}

int update_thread(void *data)
{
    /*** Main thread loop that updates frequency and TRIAC parameters ***/
    unsigned int i;
    unsigned int period_ns;
    unsigned int local_freqx100;
    int timeout_val;
    bool phase_sync;
    ktime_t local_period_time;
    ktime_t latency = THREAD_LATENCY_TIMEOUT;
    struct triac_phase local_phase;


	while(1) {
        //Go to sleep till IRQ wakes me up or timeout
        set_current_state(TASK_UNINTERRUPTIBLE);
        timeout_val = schedule_hrtimeout(&latency, HRTIMER_MODE_REL);
        if(kthread_should_stop()) break;
        if(!timeout_val) { //IRQ did not occur, lost of sync
            phase_sync = false;
            local_freqx100 = 0;
        }
        else { //In sync
            local_period_time = acline_phase.period_time;
            period_ns = (unsigned int)ktime_to_ns(local_period_time);
            if(period_ns > MIN_PERIOD_ns && period_ns < MAX_PERIOD_ns) { //Calculation limited to normal Hz boundary
                local_freqx100 = SEC_TO_NANOSEC / (period_ns/100); //Hz x 100 so don't lose precision
                phase_sync = true;
            }
            else { //If frequency is out of bounds, clear sync flag
                local_freqx100 = 0;
                phase_sync = false;
            }
        }
        
        mutex_lock(&freqx100_lock); //Lock mutex while writing value
        freqx100 = local_freqx100;
        mutex_unlock(&freqx100_lock);
		
        
        ///// TRIAC parameter update and basic on/off functions
        for(i=0; i<(triac_vector_len - 1); i++) {
            if(triac[i].gpio_status == enabled) {
                local_phase.pos_ns = 0;
                local_phase.neg_ns = 0;
                mutex_lock(&triac[i].phase_lock); //Lock mutex while reading value
                local_phase.status = triac[i].phase.status;
                local_phase.pos = triac[i].phase.pos;
                local_phase.neg = triac[i].phase.neg;
                mutex_unlock(&triac[i].phase_lock);
                switch(local_phase.status) { // Status state-machine (off, on, sym, asym)
                    case off:   {
                        if(local_phase.pos >= (180 - PHASE_GUARD) && local_phase.neg >= (180 - PHASE_GUARD)) { //Change state to ON
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = on;
                            triac[i].phase.pos = 180;
                            triac[i].phase.neg = 180;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 1); //Activate GPIO
                            break;
                        }
                        
                        if(phase_sync) { //sync is requisite to change to symmetric or asymmetric mode
                            if(local_phase.pos < (180 - PHASE_GUARD) && local_phase.pos > (0 + PHASE_GUARD) && local_phase.neg == local_phase.pos) { //Change state to SYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = sym;
                                mutex_unlock(&triac[i].phase_lock);
                                triac_phase_thread_start(i);
                                break;
                            }
                            if(local_phase.pos != local_phase.neg) { //Change state to ASYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = asym;
                                mutex_unlock(&triac[i].phase_lock);
                                triac_phase_thread_start(i);
                                break;
                            }
                        }
                        
                        //no state change
                        break;
                    }
                    
                    case on:    {
                        if(local_phase.pos <= (0 + PHASE_GUARD) && local_phase.neg <= (0 + PHASE_GUARD)) { //Change state to OFF
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = off;
                            triac[i].phase.pos = 0;
                            triac[i].phase.neg = 0;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 0); //Clear GPIO
                            break;
                        }
                        
                        if(phase_sync) { //sync is requisite to change to symmetric or asymmetric mode
                            if(local_phase.pos < (180 - PHASE_GUARD) && local_phase.pos > (0 + PHASE_GUARD) && local_phase.neg == local_phase.pos) { //Change state to SYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = sym;
                                mutex_unlock(&triac[i].phase_lock);
                                triac_phase_thread_start(i);
                                break;
                            }
                            if(local_phase.pos != local_phase.neg) { //Change state to ASYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = asym;
                                mutex_unlock(&triac[i].phase_lock);
                                triac_phase_thread_start(i);
                                break;
                            }
                        }
                        
                        //no state change
                        break;
                    }
                    
                    case sym:   {
                        if(local_phase.pos <= (0 + PHASE_GUARD) && local_phase.neg <= (0 + PHASE_GUARD)) { //Change state to OFF
                            triac_phase_thread_end(i);
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = off;
                            triac[i].phase.pos = 0;
                            triac[i].phase.neg = 0;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 0); //Clear GPIO
                            break;
                        }
                        
                        if(local_phase.pos >= (180 - PHASE_GUARD) && local_phase.neg >= (180 - PHASE_GUARD)) { //Change state to ON
                            triac_phase_thread_end(i);
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = on;
                            triac[i].phase.pos = 180;
                            triac[i].phase.neg = 180;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 1); //Activate GPIO
                            break;
                        }
                        
                        if(phase_sync) { //sync is requisite to change to asymmetric mode
                            if(local_phase.pos != local_phase.neg) { //Change state to ASYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = asym;
                                mutex_unlock(&triac[i].phase_lock);
                                break;
                            }
                            //no state change, calculate delays
                            local_phase.pos_ns = (180 - local_phase.pos) * period_ns / 360;
                            //TODO: pasar a atomic
                            triac[i].phase.pos_ns = local_phase.pos_ns;
                            triac[i].phase.neg_ns = local_phase.pos_ns;
                        }
                        break;
                    }
                    
                    case asym:  {
                        if(local_phase.pos <= (0 + PHASE_GUARD) && local_phase.neg <= (0 + PHASE_GUARD)) { //Change state to OFF
                            triac_phase_thread_end(i);
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = off;
                            triac[i].phase.pos = 0;
                            triac[i].phase.neg = 0;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 0); //Clear GPIO
                            break;
                        }
                        
                        if(local_phase.pos >= (180 - PHASE_GUARD) && local_phase.neg >= (180 - PHASE_GUARD)) { //Change state to ON
                            triac_phase_thread_end(i);
                            mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                            triac[i].phase.status = on;
                            triac[i].phase.pos = 180;
                            triac[i].phase.neg = 180;
                            mutex_unlock(&triac[i].phase_lock);
                            gpio_set_value(triac[i].gpio, 1); //Activate GPIO
                            break;
                        }
                        
                        if(phase_sync) { //sync is requisite to change to symmetric mode
                            if(local_phase.pos < (180 - PHASE_GUARD) && local_phase.pos > (0 + PHASE_GUARD) && local_phase.neg == local_phase.pos) { //Change state to SYM
                                mutex_lock(&triac[i].phase_lock); //Lock mutex while writing value
                                triac[i].phase.status = sym;
                                mutex_unlock(&triac[i].phase_lock);
                                break;
                            }
                            //no state change, calculate delays
                            if(local_phase.pos <= (0 + PHASE_GUARD)) local_phase.pos = 180; //Force pulse skip (180 - 180) = 0ns
                            else if(local_phase.pos >= (180 - PHASE_GUARD)) local_phase.pos = 180 - PHASE_GUARD; //Bound edge values
                            local_phase.pos_ns = (180 - local_phase.pos) * period_ns / 360;
                            if(local_phase.neg <= (0 + PHASE_GUARD)) local_phase.neg = 180;  //Force pulse skip (180 - 180) = 0ns
                            else if(local_phase.neg >= (180 - PHASE_GUARD)) local_phase.neg = 180 - PHASE_GUARD; //Bound edge values
                            local_phase.neg_ns = (180 - local_phase.neg) * period_ns / 360;
                            //TODO: pasar a atomic
                            triac[i].phase.pos_ns = local_phase.pos_ns;
                            triac[i].phase.neg_ns = local_phase.neg_ns;
                        }
                        break;
                    }

                    default:    break;
                } //end switch
            } //end if enabled
        } // end for
	} //end while

	//Stop all running child threads
	for(i=0; i<(triac_vector_len - 1); i++) triac_phase_thread_end(i);
	printk(KERN_INFO "update thread: stopped\n");
	return 0;
}

static inline int triac_phase_thread_start(unsigned int param)
{
    static unsigned int i; //ATTN! if not static, 'i' can be overwritten before thread catches it
    i = param;
    
    
    if(IS_ERR_OR_NULL(triac[i].phase_task)) { //task pointer uninitialized, initialize it
        triac[i].phase_task = kthread_create(triac_phase_thread, &i, triac[i].label);
        if(IS_ERR_OR_NULL(triac[i].phase_task)) {
            printk(KERN_ERR "%s thread: could not run\n", triac[i].label);
            return -ENOMEM;
        }
        
        struct sched_param thread_param = {.sched_priority = MAX_RT_PRIO - 50};
        sched_setscheduler(triac[i].phase_task, SCHED_FIFO, &thread_param);
        wake_up_process(triac[i].phase_task);
    }
    
    else if(!pid_alive(triac[i].phase_task)) { //PID invalid, recreate task
        triac[i].phase_task = kthread_create(triac_phase_thread, &i, triac[i].label);
        
        if(IS_ERR_OR_NULL(triac[i].phase_task)) {
            printk(KERN_ERR "%s thread: could not run\n", triac[i].label);
            return -ENOMEM;
        }
        
        struct sched_param thread_param = {.sched_priority = MAX_RT_PRIO - 50};
        sched_setscheduler(triac[i].phase_task, SCHED_FIFO, &thread_param);
        wake_up_process(triac[i].phase_task);
    }
    
    return 0;
}

static inline void triac_phase_thread_end(unsigned int i)
{
    /*Protected way of stopping threads. Not NULL pointer, pid_alive*/
    if(!IS_ERR_OR_NULL(triac[i].phase_task)) {
        if(pid_alive(triac[i].phase_task)) {
            //wake_up_process(triac[i].phase_task); //Wake up thread if it was sleeping
            kthread_stop(triac[i].phase_task); //Send STOP signal
        }
    }
}

int triac_phase_thread(void *data)
{
    /*** Per TRIAC thread, each one doing phase modulation ***/
    unsigned int i;
    i = *(unsigned int *)data;
    ktime_t my_timestamp;
    ktime_t error_timestamp;
    ktime_t half_period_time;
    ktime_t pos_trigger_time;
    ktime_t neg_trigger_time;
    ktime_t latency = THREAD_LATENCY_TIMEOUT;
    int timeout_val;
    struct acline_time local_acline_phase;
    struct triac_phase local_phase;
    
    if(i >= triac_vector_len - 1) { //Check for overflow, excludes ACLINE
        printk(KERN_ERR "TRIAC: pointer overflow %u\n", i);
        return -1;
    }
    
    printk(KERN_INFO "%s thread: started\n", triac[i].label);

	while(1) {
        local_phase.pos_ns = triac[i].phase.pos_ns;
        local_phase.neg_ns = triac[i].phase.neg_ns;
        
        set_current_state(TASK_UNINTERRUPTIBLE);
        timeout_val = schedule_hrtimeout(&latency, HRTIMER_MODE_REL);
        if(kthread_should_stop() || !timeout_val) break; //If timeout or thread_stop, quit

        //No need to mutex here. Interrupt won't trigger for another 20ms
        local_acline_phase = acline_phase;
        half_period_time = local_acline_phase.period_time / 2;
        
        if(local_phase.neg_ns) { //Only cycle if time is calculated
            //Begin TRIAC trigger on negative cycle
            my_timestamp = ktime_get(); //Get new timestamp for time error calculation
            error_timestamp = ktime_sub(my_timestamp, local_acline_phase.timestamp); //Calculate latency between IRQ and now
            neg_trigger_time = ktime_sub(ktime_set(0, local_phase.neg_ns + OPTO_HYSTERESIS_TIME), error_timestamp);
            set_current_state(TASK_UNINTERRUPTIBLE);
            schedule_hrtimeout(&neg_trigger_time, HRTIMER_MODE_REL); //Go to sleep
            gpio_set_value(triac[i].gpio, 1); //Turn on TRIAC
            if(local_phase.neg_ns < HIGH_CONDUCTION_ANGLE) usleep_range(TRIAC_LONG_PULSE,TRIAC_LONG_PULSE*2); //For high conduction angles, uses longer TRIAC triggering
            else udelay(TRIAC_TRIGGER_PULSE);
            gpio_set_value(triac[i].gpio, 0); //Turn off TRIAC
        }
        
        if(local_phase.pos_ns) { //Only cycle if time is calculated
            /*Begin TRIAC trigger on positive cycle*/
            my_timestamp = ktime_get(); //Get new timestamp for time error calculation
            error_timestamp = ktime_sub(my_timestamp, local_acline_phase.timestamp); //Calculate latency between IRQ and now
            pos_trigger_time = ktime_add_ns(ktime_sub(half_period_time, error_timestamp), local_phase.pos_ns + OPTO_HYSTERESIS_TIME);
            set_current_state(TASK_UNINTERRUPTIBLE);
            schedule_hrtimeout(&pos_trigger_time, HRTIMER_MODE_REL); //Go to sleep
            gpio_set_value(triac[i].gpio, 1); //Turn on TRIAC
            if(local_phase.pos_ns < HIGH_CONDUCTION_ANGLE) usleep_range(TRIAC_LONG_PULSE,TRIAC_LONG_PULSE*2); //For high conduction angles, uses longer TRIAC triggering
            else udelay(TRIAC_TRIGGER_PULSE);
            gpio_set_value(triac[i].gpio, 0); //Turn off TRIAC
        }
	}
	
	printk(KERN_INFO "%s thread: stopped\n", triac[i].label);
	return 0;
}


/*********************** IRQ ***********************/
static int irq_start(void)
{   
    int retval = -EIO; //Start with error
    irqNumber = gpio_to_irq(ACLINE);
    
    if(request_threaded_irq(irqNumber, (irq_handler_t)gpio_irq_handler, (irq_handler_t)gpio_irq_handler_thread, IRQF_TRIGGER_RISING, "lineAC", NULL)) {
        printk(KERN_ERR "IRQ %d: could not request\n", irqNumber);
    }
    else retval = 0; //IRQ configured. Clear error
    
    return retval;
}
    
static void irq_end(void)
{
    free_irq(irqNumber, NULL);
}

static irq_handler_t gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    //Check if interrupt was due to ACLINE change
    if(gpio_get_value(ACLINE)) {
        //Make time calculations
        acline_phase.old_timestamp = acline_phase.timestamp; //Backup old timestamp for period calculation
        acline_phase.timestamp = ktime_get(); //Get new timestamp
        acline_phase.period_time = ktime_sub(acline_phase.timestamp, acline_phase.old_timestamp); //Calculate time difference
        
        //Calculations ready, wake up update task
        if(!IS_ERR_OR_NULL(update_task) && pid_alive(update_task)) wake_up_process(update_task); //Now we have OPTO_HYSTERESIS_TIME usec to perform complex calculations
        return (irq_handler_t)IRQ_WAKE_THREAD;
    }
    else return (irq_handler_t)IRQ_HANDLED;
}

static irq_handler_t gpio_irq_handler_thread(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    unsigned int i;
    ktime_t delay = ktime_set(0, OPTO_HYSTERESIS_TIME);
    
    schedule_hrtimeout(&delay, HRTIMER_MODE_REL); //Go to sleep, then wake up triac phase threads
    
    //Wake up TRIAC threads if active
    for(i=0; i < (triac_vector_len - 1); i++) { //Excludes ACLINE
        if(!IS_ERR_OR_NULL(triac[i].phase_task)) {
            if(pid_alive(triac[i].phase_task)) wake_up_process(triac[i].phase_task);
        }
    }
    
    return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}


/********************** GPIO *********************/
static int gpio_start(void)
{
    unsigned int i; 
    int retval = -EIO; //Start with error
    
    for(i=0; i < triac_vector_len; i++) {
        if(triac[i].gpio_status != disabled) {
            if(gpio_request_one(triac[i].gpio, triac[i].gpio_flags, triac[i].label)) {
                printk(KERN_ERR "%s: GPIO error\n", triac[i].label);
                triac[i].gpio_status = error;
            }
            else {
                triac[i].gpio_status = enabled;
                triac[i].phase.status = off;
                triac[i].phase.pos = 0;
                triac[i].phase.neg = 0;
                retval = 0; //At least one GPIO configured. Clear error
            }
        }
    }
    
    return retval;
}

static void gpio_end(void)
{
    unsigned int i;
    
    for(i=0; i < triac_vector_len; i++) { //GPIO release
        if(triac[i].gpio_status != disabled) {
            gpio_free(triac[i].gpio);
            printk(KERN_INFO "%s: GPIO %d released\n", triac[i].label, triac[i].gpio);
        }
    }
}


/********************** MODULE *********************/
static int __init triac_init(void)
{
    
    int err;
    bool degraded_irq = false;
    
	printk(KERN_INFO "Opto-TRIAC Board: Initializing...\n");
    
    err = gpio_start();
    if(err) goto fail_gpio; //Critical fail
    
    err = sysfs_start();
    if(err) goto fail_sysfs; //Critical fail
    
    err = update_thread_start();
    if(err) goto fail_thread; //Critical fail
    
    err = irq_start();
    if(err) degraded_irq = true; //Non-critical fail. Continue in degraded mode.
    
    printk(KERN_INFO "Opto-TRIAC Board: READY!\n");
    return 0;
    
    fail_thread:    sysfs_end();
    fail_sysfs:     gpio_end();
    fail_gpio:      return err;
}


static void __exit triac_exit(void)
{
    
	printk(KERN_INFO "Opto-TRIAC Board: Stopping...\n");
	
    irq_end();
    update_thread_end();
    sysfs_end();
    gpio_end();
    
	printk(KERN_INFO "Opto-TRIAC Board: STOPPED\n");
}

module_init(triac_init);
module_exit(triac_exit);
