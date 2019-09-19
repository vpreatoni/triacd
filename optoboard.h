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

#include "globals.h"

/* PIN defaults in case cannot read EEPROM */
#define DEFAULT_ACLINE_PIN			5
#define DEFAULT_TRIAC1_PIN			26
#define DEFAULT_TRIAC2_PIN			19
#define DEFAULT_TRIAC3_PIN			13
#define DEFAULT_TRIAC4_PIN			6

/* TODO: Move this to .conf */
#define DEFAULT_TRIAC_NAME			"TRIAC"

extern int hat_get_next_in(void);
extern int hat_get_next_out(void);
extern int hat_read_eeprom(void);


int board_start_acline(unsigned int);
void board_stop_acline(void);
unsigned int board_init_channels(void);
void board_free_channels(void);
int board_start_triacdrv(unsigned int, unsigned int, char *);
void board_stop_triacdrv(unsigned int);

#endif //OPTOBOARD_H
