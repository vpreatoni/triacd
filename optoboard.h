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


/* Where to print messages */
#define FPRINTF_FD					stdout
/* PIN defaults in case cannot read EEPROM */
#define DEFAULT_ACLINE_PIN			5
#define DEFAULT_TRIAC1_PIN			26
#define DEFAULT_TRIAC2_PIN			19
#define DEFAULT_TRIAC3_PIN			13
#define DEFAULT_TRIAC4_PIN			6
/* Board properties */
#define MAX_TRIACS					4


/* TODO: Move this to .conf */
#define DEFAULT_TRIAC_NAME			"TRIAC"
#define MODULE_DIR					"/sys/triacd"



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

struct triac_status triac[MAX_TRIACS];


extern int hat_get_next_in(void);
extern int hat_get_next_out(void);
extern int hat_read_eeprom(void);
extern void fader_start(unsigned int, unsigned int, unsigned int, unsigned int);
extern void fader_stop(unsigned int);
extern void fader_init(struct triac_status *);

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
