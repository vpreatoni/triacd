#ifndef OPTOBOARD_H
#define OPTOBOARD_H

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>


/* Where to print messages */
#define FPRINTF_FD					stdout
/* Kernel module sysfs node */
#define MODULE_DIR				"/sys/triacd"
/* HAT device-tree node */
#define HAT_DIR					"/proc/device-tree/triacboard"
#define HAT_INPUTS_DIR			"/in"
#define HAT_OUTPUTS_DIR			"/out"
#define HAT_VENDOR_FILE			"vendor"
#define HAT_PRODUCT_FILE		"product"
#define HAT_VERSION_FILE		"version"
#define HAT_GPIO_LABEL			"label"
#define HAT_GPIO_PIN			"arm_gpio"
#define HAT_IO_CHANNELS			"channels"

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

struct triac_status *triac;
unsigned int triac_status_len;


extern void fader_start(unsigned int, unsigned int, unsigned int, unsigned int);
extern void fader_stop(unsigned int);
extern void fader_init(struct triac_status *, unsigned int);
extern void fader_release(void);

int board_start_acline(unsigned int);
void board_stop_acline(void);
unsigned int board_init_channels(void);
void board_free_channels(void);
int board_start_triacdrv(unsigned int, unsigned int, char *);
void board_stop_triacdrv(unsigned int);
void board_update_channel(unsigned int, bool, unsigned int, unsigned int, unsigned int);

void statem_loop(void);
int statem_send_command(char *, unsigned int, unsigned int);
void statem_set_off(unsigned int);
void statem_set_on(unsigned int);
void statem_set_sym(unsigned int, unsigned int);
void statem_set_asym(unsigned int, unsigned int, unsigned int);


#endif //OPTOBOARD_H
