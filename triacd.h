#ifndef TRIACD_H
#define TRIACD_H

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

// #include "globals.h"

/* Board properties */
#define MAX_TRIACS				4
/* Where to print messages */
#define FPRINTF_FD				stdout
/* Message queue name for client-daemon IPC */
#define QUEUE_NAME				"/triacd_q"
#define BUFF_SIZE    			128
/* Time constants */
#define USEC_TO_NANOSEC			1000U
#define MSEC_TO_NANOSEC			(1000U * USEC_TO_NANOSEC)
#define SEC_TO_NANOSEC			(1000U * MSEC_TO_NANOSEC)
#define MSEC_TO_USEC			1000U
#define THREAD_LATENCY			(100U * MSEC_TO_USEC)


extern unsigned int board_init_channels(void);
extern void board_free_channels(void);
extern void board_update_channel(unsigned int, bool, unsigned int, unsigned int, unsigned int);
extern void statem_loop(void);


static unsigned int max_channels;

/* Signal handler stop request */
static volatile sig_atomic_t daemon_stop = 0;

/* Message queue struct */
struct triac_data {
	unsigned int channel;
	bool fade;
	unsigned int time;
	unsigned int pos;
	unsigned int neg;
};

/* Union to "serialize" struct triac_data */
union msg_q {
	struct triac_data triac;
	char message[sizeof(struct triac_data)];
};

void triacd_sigterm(int);
int triacd_main_loop(void);
int triacd_set_params(int, bool, int, int, int);
void triacd_refresh_params(struct triac_data);
void triacd_init_signals(void);
mqd_t triacd_init_mq(void);
void triacd_end_mq(mqd_t);
void triacd_print_params(char *);

#endif //TRIACD_H
