#include "optoboard.h"


/* Loads aclinedrv kernel module */
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

/* Loads triacNdrv kernel module */
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

/* Function that sets channel parameters
 * This function is used to avoid exposing struct triac_status to triacd.c
 * triacd.c will parse required parameters and pass them to us
 */
void board_update_channel(unsigned int n, bool fade, unsigned int time, unsigned int pos, unsigned int neg)
{
	unsigned int i = n - 1;
	 
	if (triac[i].gpio.status == enabled) {
// 		fprintf(FPRINTF_FD, "%s request:\n", triac[i].gpio.label);
		if (fade) {
// 			fprintf(FPRINTF_FD, "\tfading time %ums\n", time);
			fader_start(i, time, pos, neg);
		}
		else {
			fader_stop(i);
			triac[i].phase.pos = pos;
			triac[i].phase.neg = neg;
			triac[i].phase.refresh = true;
		}
// 		fprintf(FPRINTF_FD, "\tpos phase %udeg\n", pos);
// 		fprintf(FPRINTF_FD, "\tneg phase %udeg\n", neg);
	}
	
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
	unsigned int i, channels;
	
	/* initializes global variables */
	for (i = 0; i < MAX_TRIACS; i++) {
		triac[i].gpio.status = disabled;
		triac[i].phase.refresh = false;
	}
	
	fader_init(triac);
	
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
		for (channels = 0, i = 0; i < MAX_TRIACS; i++) {
			triac[i].phase.pos = 0;
			triac[i].phase.neg = 0;
			triac[i].phase.status = off;
			sprintf(triac[i].gpio.label, "%s%u", DEFAULT_TRIAC_NAME, i + 1);
			if (board_start_triacdrv(i + 1, triac[i].gpio.pin, triac[i].gpio.label)) {
				fprintf(FPRINTF_FD, "board_init_channels: error - cannot start triac%udrv module\n", i + 1);
				triac[i].gpio.status = error;
			}
			else {
				triac[i].gpio.status = enabled;
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
		
		for (channels = 0, i = 0; i < MAX_TRIACS; i++) {
			if ((pin = hat_get_next_out()) != -1) {
				fprintf(FPRINTF_FD, "board_init_channels: output pin found: %d\n", pin);
				triac[i].gpio.pin = pin;
				triac[i].phase.pos = 0;
				triac[i].phase.neg = 0;
				triac[i].phase.status = off;
				sprintf(triac[i].gpio.label, "%s%u", DEFAULT_TRIAC_NAME, i + 1);
				if (board_start_triacdrv(i + 1, triac[i].gpio.pin, triac[i].gpio.label)) {
					fprintf(FPRINTF_FD, "board_init_channels: error - cannot start triac%udrv module\n", i + 1);
					triac[i].gpio.status = error;
				}
				else {
					triac[i].gpio.status = enabled;
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
	unsigned int i;
	
	for (i = 0; i < MAX_TRIACS; i++) {
		if (triac[i].gpio.status == enabled) {
			fader_stop(i);
			board_stop_triacdrv(i + 1);
			triac[i].gpio.status = disabled;
			fprintf(FPRINTF_FD, "board_free_channels: channel %u released\n", i + 1);
		}
	}
	
	board_stop_acline();
	
	return;
}



/* Sends command to /sysfs triac channel */
int statem_send_command(char *name, unsigned int pos, unsigned int neg)
{
	int fd;
	char params[128];
	char filename[128];
	
	sprintf(params, "%u %u", pos, neg);
	sprintf(filename, "%s/%s", MODULE_DIR, name);
	fd = open(filename, O_WRONLY);
	if (write(fd, params, strlen(params)) <= 0) {
		fprintf(FPRINTF_FD, "statem_send_command error: %d - %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}
// 	fprintf(FPRINTF_FD, "statem_send_command: %s %s\n", filename, params);
	close(fd);
	
	return 0;
}

void statem_set_off(unsigned int i)
{
	triac[i].phase.status = off;
	triac[i].phase.pos = 0;
	triac[i].phase.neg = 0;
	statem_send_command(triac[i].gpio.label, 0, 0);
	
	return;
}

void statem_set_on(unsigned int i)
{
	
	triac[i].phase.status = on;
	triac[i].phase.pos = 180;
	triac[i].phase.neg = 180;
	statem_send_command(triac[i].gpio.label, 180, 180);
	
	return;
}

void statem_set_sym(unsigned int i, unsigned int phase)
{
	triac[i].phase.status = sym;
	statem_send_command(triac[i].gpio.label, phase, phase);
	
	return;
}

void statem_set_asym(unsigned int i, unsigned int pos, unsigned int neg)
{
	triac[i].phase.status = asym;
	statem_send_command(triac[i].gpio.label, pos, neg);
	
	return;
}

/* State machine
 * Parses phase.status and changes state
 * according to phase values
 * Possible states:
 * 		on		triac fully on (180 deg)
 * 		off		triac fully off (0 deg)
 * 		sym		symmetic phase control (negative == positive)
 * 		ssym	asymmetic phase control (negative != positive)
 */
void statem_loop(void)
{
	unsigned int i;
	unsigned int local_pos, local_neg;
	
	for (i = 0; i < MAX_TRIACS; i++) {
		if (triac[i].gpio.status == enabled) {
			local_pos = triac[i].phase.pos;
			local_neg = triac[i].phase.neg;


			switch (triac[i].phase.status) {
				case off:
					if (local_pos == 180 && local_neg == 180) {
						statem_set_on(i);
						break;
					}
					
					if (local_pos < 180 && local_pos > 0) {
						if (local_neg == local_pos) {
							statem_set_sym(i, local_pos);
							break;
						}
						else {
							statem_set_asym(i, local_pos, local_neg);
							break;
						}
					}
					
					/* no state change */
					break;
					
				case on:
					if (local_pos == 0 && local_neg == 0) { 
						statem_set_off(i);
						break;
					}
					
					if (local_pos < 180 && local_pos > 0) {
						if (local_neg == local_pos) {
							statem_set_sym(i, local_pos);
							break;
						}
						else {
							statem_set_asym(i, local_pos, local_neg);
							break;
						}
					}
					
					/* no state change */
					break;
					
				case sym:
					if (local_pos == 0 && local_neg == 0) {
						statem_set_off(i);
						break;
					}
					
					if (local_pos == 180 && local_neg == 180) {
						statem_set_on(i);
						break;
					}
					
					if (local_pos != local_neg) {
						statem_set_asym(i, local_pos, local_neg);
						break;
					}
					
					/* no state change */
					if (triac[i].phase.refresh) {
						statem_set_sym(i, local_pos);
						triac[i].phase.refresh = false;
					}
					break;
					
				case asym:
					if (local_pos == 0 && local_neg == 0) {
						statem_set_off(i);
						break;
					}
					
					if (local_pos == 180 && local_neg == 180) {
						statem_set_on(i);
						break;
					}
					
					if (local_neg == local_pos) {
						statem_set_sym(i, local_pos);
						break;
					}
					
					/* no state change */
					if (triac[i].phase.refresh) {
						statem_set_asym(i, local_pos, local_neg);
						triac[i].phase.refresh = false;
					}
					break;
					
				default:
					break;
			} //end switch
		} //end if enabled
	} //end for
	
	return;
}
