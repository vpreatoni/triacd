#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/sched/types.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Preatoni");
MODULE_DESCRIPTION("OpenIndoor Opto-TRIAC Board driver");
MODULE_VERSION("0.1");
MODULE_INFO(intree, "Y");
#define SEC_TO_NANOSEC  1000000000U
#define MSEC_TO_NANOSEC  1000000U
#define USEC_TO_NANOSEC  1000U


/**************** IRQ *******************/
#define OPTO_HYSTERESIS_TIME    260U*USEC_TO_NANOSEC //us
static unsigned int irqNumber;
static struct acline_time {
    ktime_t timestamp;
    ktime_t old_timestamp;
    ktime_t period_time;
} acline_phase;


static int irq_start(void);
static void irq_end(void);
static irq_handler_t gpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t gpio_irq_handler_thread(unsigned int irq, void *dev_id, struct pt_regs *regs);

/**************** SYSFS *****************/
#define SYSFS_NODE  "triacboard"

static struct kobject *triac_kobject;

static int sysfs_start(void);
static void sysfs_end(void);
static ssize_t set_triac(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count);
static ssize_t get_triac(struct kobject *kobj, struct kobj_attribute *attr, char *buff);
static ssize_t get_acline(struct kobject *kobj, struct kobj_attribute *attr, char *buff);


/***************** GPIO ********************/

//ARM GPIO pin number
#define TRIAC1	26
#define TRIAC2	19
#define TRIAC3	13
#define TRIAC4	6
#define ACLINE  5
#define MIN_FREQUENCY   40U //Hz
#define MAX_FREQUENCY   70U //Hz
#define MIN_PERIOD_ns      SEC_TO_NANOSEC/MAX_FREQUENCY
#define MAX_PERIOD_ns      SEC_TO_NANOSEC/MIN_FREQUENCY

#define HIGH_CONDUCTION_ANGLE   1500U*USEC_TO_NANOSEC //us
#define TRIAC_TRIGGER_PULSE    10U //us
#define TRIAC_LONG_PULSE    500U //us
#define PHASE_GUARD    5U //degrees

enum status {off, on, sym, asym}; //sym = symmetric phase control    asym = asymmetric phase control (non zero mean value)

struct triac_phase {
    enum status status; 
    unsigned int pos; //Positive phase trigger
    unsigned int neg; //Negative phase trigger
    unsigned int pos_ns; //Positive phase trigger converted to nanoseconds
    unsigned int neg_ns; //Negative phase trigger converted to nanoseconds
};


struct triac_status {
    unsigned gpio; //ARM GPIO pin number
    unsigned long gpio_flags;
    const char label[16]; //Friendly name
    enum {disabled, error, enabled} gpio_status; //Disabled is set by hardware. Error or Enabled by software
    struct kobj_attribute sysfs;
    struct triac_phase phase; //Runtime initialized
    struct mutex phase_lock; //Mutex for phase variable
    struct task_struct *phase_task; //Phase modulation thread
    struct task_struct *fade_task; //Fade in/out thread
};

static struct triac_status triac [] = {
    {TRIAC1, GPIOF_OUT_INIT_LOW, "triac1", enabled, __ATTR(1, 0664, get_triac, set_triac)},
    {TRIAC2, GPIOF_OUT_INIT_LOW, "triac2", enabled, __ATTR(2, 0664, get_triac, set_triac)},
    {TRIAC3, GPIOF_OUT_INIT_LOW, "triac3", enabled, __ATTR(3, 0664, get_triac, set_triac)},
    {TRIAC4, GPIOF_OUT_INIT_LOW, "triac4", enabled, __ATTR(4, 0664, get_triac, set_triac)},
    {ACLINE, GPIOF_IN, "lineAC", enabled, __ATTR(freq, 0444, get_acline, NULL)}
};

static size_t triac_vector_len = ARRAY_SIZE(triac);

static int gpio_start(void);
static void gpio_end(void);

/************************ THREADS ***************************/
#define THREAD_NAME "triacdrv_loop"
#define THREAD_LATENCY_TIMEOUT 100U*MSEC_TO_NANOSEC //ms

struct task_struct *update_task;
static DEFINE_MUTEX(freqx100_lock); //Mutex for freqx100 variable
// static DEFINE_MUTEX(phase_lock); //Mutex for struct triac_phase phase variable
static unsigned int freqx100;

static int update_thread_start(void);
static void update_thread_end(void);
int update_thread(void *data);
static int triac_phase_thread_start(unsigned int channel);
static void triac_phase_thread_end(unsigned int channel);
int triac_phase_thread(void *data);
static int triac_fade_thread_start(unsigned int i, unsigned int fade_stop_phase, unsigned int fade_time);
static void triac_fade_thread_end(unsigned int i);
int fade_thread(void *data);


/*************** MODULE ******************/
static int __init triac_init(void);
static void __exit triac_exit(void);
