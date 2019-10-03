#ifndef FADER_H
#define FADER_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>


/* Where to print messages */
#define FPRINTF_FD					stdout
/* Fader thread status */
#define STOPPED						0x00
#define ABOUT_TO_STOP				0x10
#define STOPPING					0x20
#define STARTED						0x01
#define ABOUT_TO_START				0x11
/* Time constants */
#define USEC_TO_NANOSEC			1000U
#define MSEC_TO_NANOSEC			(1000U * USEC_TO_NANOSEC)
#define SEC_TO_NANOSEC			(1000U * MSEC_TO_NANOSEC)
#define MSEC_TO_USEC			1000U
#define THREAD_LATENCY			(100U * MSEC_TO_USEC)

struct triac_phase {
	volatile unsigned int pos;
	volatile unsigned int neg;
	enum {off, on, sym, asym} status;
	volatile bool refresh;
};

struct triac_gpio {
	/* ARM GPIO pin number */
	unsigned pin;
	/* TRIAC channel friendly name, can be any */
	char label[16];
	/* GPIO initialization status: disabled unless set by software */
	enum {disabled, error, enabled} status;
};

/* Main TRIAC control structure */
struct triac_status {
	struct triac_gpio gpio;
	struct triac_phase phase;
};

struct triac_fade {
	pthread_t thread;
	struct triac_phase *phase;
	unsigned int final_pos;
	unsigned int final_neg;
	unsigned int time;
	volatile unsigned int status;
};

struct triac_fade *fader;
unsigned int triac_fade_len;


void fader_start(unsigned int, unsigned int, unsigned int, unsigned int);
void fader_stop(unsigned int);
void * fader_function(void *arg);
void fader_init(struct triac_status *, unsigned int);
void fader_release(void);

#endif //FADER_H
