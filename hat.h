#ifndef HAT_H
#define HAT_H

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "eeptypes.h"

/* EEPROM defaults */
#define EEPROM_TYPE					"24c32"
#define EEPROM_SIZE					4096
#define DEFAULT_EEPROM_ADDRESS		0x50
#define I2C_ADAPTER_FILE			"/sys/class/i2c-adapter/i2c-"
/* i2c_videocore is bus 0 */
#define I2C_BUS						0
/* Max number of in / out pins board may have */
#define MAX_PINS					4
/* Where to print messages */
#define FPRINTF_FD					stdout

/* Global struct to fill while reading EEPROM */
static struct hat_info {
	char *vendor;
	char *product;
	uint16_t version;
	int in_pins[MAX_PINS];
	int out_pins[MAX_PINS];
} hat_board;

int hat_start_modules(void);
void hat_stop_modules(void);
int hat_init_eeprom(void);
void hat_free_eeprom(void);
int hat_read_eeprom(void);
int hat_get_next_in(void);
int hat_get_next_out(void);


#endif //HAT_H
