#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdatomic.h>
#include <stdbool.h>

/* Fader thread status */
#define STOPPED					0x00
#define ABOUT_TO_STOP			0x10
#define STOPPING				0x20
#define STARTED					0x01
#define ABOUT_TO_START			0x11

/* Board properties */
#define MAX_TRIACS				4


/* Where to print messages */
#define FPRINTF_FD				stdout


struct triac_phase {
	atomic_uint pos;
	atomic_uint neg;
	enum {off, on, sym, asym} status;
};

struct triac_gpio {
	/* ARM GPIO pin number */
	unsigned pin;
	/* TRIAC channel friendly name, can be any */
	char label[16];
	/* GPIO initialization status: disabled unless set by software */
	enum {disabled, error, enabled} status;
};

struct triac_fade {
	struct task_struct *task;
	atomic_uint remaining_time;
	atomic_uint status;
};

/* Main TRIAC control structure */
struct triac_status {
	struct triac_gpio gpio;
	struct triac_phase phase;
	struct triac_fade fader;
};

//TODO move to optoboard.h and make extern!!
struct triac_status triac[MAX_TRIACS];

#endif //GLOBALS_H
