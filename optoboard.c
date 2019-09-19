#include "optoboard.h"

/* Loads aclinedrv.ko located on ./kmod folder */
int board_start_acline(unsigned int pin)
{
	char module[128];

	sprintf(module, "modprobe aclinedrv opto_input=%u", pin);
	
	if (system(module))
		return EXIT_FAILURE;
	
	return 0;
}

void board_stop_acline(void)
{
	system("rmmod aclinedrv");
	return;
}

int board_start_triacdrv(unsigned int n, unsigned int pin, char *name)
{
	char module[128];
	
	sprintf(module, "modprobe triac%udrv gpio=%u pos=%u neg=%u name=%s", n, pin, 0, 0, name);
	
	if (system(module))
		return EXIT_FAILURE;
	
	return 0;
}

void board_stop_triacdrv(unsigned int n)
{
	char module[128];
	
	sprintf(module, "rmmod triac%udrv", n);
	system(module);
	return;
}

/* Reads EEPROM and initializes struct triac_status.gpio
 * according to EEPROM values, or to default ones if no EEPROM
 * could be read.
 * 
 * Returns the number of configured channels [0-4].
 * User should catch 0, since means NO channels available.
 */
unsigned int board_init_channels(void)
{
	int pin;
	unsigned int j, channels;
	
	/* initializes global variable */
	for (j = 0; j < MAX_TRIACS; j++)
		triac[j].gpio.status = disabled;
	
	if (hat_read_eeprom()) {
		/* Failed to read EEPROM */
		fprintf(FPRINTF_FD, "board_init_channels: no EEPROM could be read. Assuming defaults\n");
		if (board_start_acline(DEFAULT_ACLINE_PIN))
			/* Error */
			return 0;
		
		triac[0].gpio.pin = DEFAULT_TRIAC1_PIN;
		triac[1].gpio.pin = DEFAULT_TRIAC2_PIN;
		triac[2].gpio.pin = DEFAULT_TRIAC3_PIN;
		triac[3].gpio.pin = DEFAULT_TRIAC4_PIN;
		for (channels = 0, j = 0; j < MAX_TRIACS; j++) {
			triac[j].phase.pos = 0;
			triac[j].phase.neg = 0;
			triac[j].phase.status = off;
			sprintf(triac[j].gpio.label, "%s%u", DEFAULT_TRIAC_NAME, j + 1);
			if (board_start_triacdrv(j + 1, triac[j].gpio.pin, triac[j].gpio.label)) {
				fprintf(FPRINTF_FD, "board_init_channels: error - cannot start triac%udrv module\n", j + 1);
				triac[j].gpio.status = error;
			}
			else {
				triac[j].gpio.status = enabled;
				channels++;
			}
		}
	}
	else {
		/* EEPROM read successful */
		pin = hat_get_next_in();
		if (pin != -1)
			fprintf(FPRINTF_FD, "board_init_channels: input pin found: %d\n", pin);
		else pin = DEFAULT_ACLINE_PIN;
		
		if (board_start_acline(pin))
			/* Error */
			return 0;
		
		for (channels = 0, j = 0; j < MAX_TRIACS; j++) {
			if ((pin = hat_get_next_out()) != -1) {
				fprintf(FPRINTF_FD, "board_init_channels: output pin found: %d\n", pin);
				triac[j].gpio.pin = pin;
				triac[j].phase.pos = 0;
				triac[j].phase.neg = 0;
				triac[j].phase.status = off;
				sprintf(triac[j].gpio.label, "%s%u", DEFAULT_TRIAC_NAME, j + 1);
				if (board_start_triacdrv(j + 1, triac[j].gpio.pin, triac[j].gpio.label)) {
					fprintf(FPRINTF_FD, "board_init_channels: error - cannot start triac%udrv module\n", j + 1);
					triac[j].gpio.status = error;
				}
				else {
					triac[j].gpio.status = enabled;
					channels++;
				}
			}
		}
	}
	
	return channels;
}

/* Releases all channels, stops Kernel modules */
void board_free_channels(void)
{
	unsigned int j;
	
	for (j = 0; j < MAX_TRIACS; j++) {
		if (triac[j].gpio.status == enabled) {
			board_stop_triacdrv(j + 1);
			triac[j].gpio.status = disabled;
			fprintf(FPRINTF_FD, "board_free_channels: channel %u released\n", j + 1);
		}
	}
	
	board_stop_acline();
	
	return;
}
