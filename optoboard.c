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
		if (fade)
			if (!time)
				fader_stop(i);
			else
				fader_start(i, time, pos, neg);
		else {
			fader_stop(i);
			triac[i].phase.pos = pos;
			triac[i].phase.neg = neg;
			triac[i].phase.refresh = true;
		}
	}
	
	return;
}

/* Reads HAT and initializes struct triac_status.gpio
 * according to HAT Devie-Tree parameters.
 * 
 * Returns the number of configured channels [0-4].
 * User should catch 0, since means NO channels available.
 */
unsigned int board_init_channels(void)
{
	unsigned int i, channels, version, gpio_pin;
	
	int fd;
	char buffer[128];
	char filename[128];
	
	/* Read vendor string */
	sprintf(filename, "%s/%s", HAT_DIR, HAT_VENDOR_FILE);
	fd = open(filename, O_RDONLY);
	if (read(fd, buffer, sizeof(buffer)) == -1)
		goto read_error;
	else
		fprintf(FPRINTF_FD, "%s HAT detected!\n", buffer);
	close(fd);
	
	/* Read product string */
	sprintf(filename, "%s/%s", HAT_DIR, HAT_PRODUCT_FILE);
	fd = open(filename, O_RDONLY);
	if (read(fd, buffer, sizeof(buffer)) == -1)
		goto read_error;
	else
		fprintf(FPRINTF_FD, "\t%s", buffer);
	close(fd);

	/* Read HAT version uint32 */
	sprintf(filename, "%s/%s", HAT_DIR, HAT_VERSION_FILE);
	fd = open(filename, O_RDONLY);
	if (read(fd, &version, sizeof(version)) == -1)
		goto read_error;
	else
		fprintf(FPRINTF_FD, "\tv%u.%u\n", (ntohl(version) & 0x0000FF00) >> 8, (ntohl(version) & 0x000000FF));
	close(fd);

	
	/* Read input channels uint32 */
	sprintf(filename, "%s/%s/%s", HAT_DIR, HAT_INPUTS_DIR, HAT_IO_CHANNELS);
	fd = open(filename, O_RDONLY);
	if (read(fd, &channels, sizeof(channels)) == -1)
		goto read_error;
	close(fd);
	
	/* Read input channel N pin */
	sprintf(filename, "%s/%s/%u/%s", HAT_DIR, HAT_INPUTS_DIR, ntohl(channels), HAT_GPIO_PIN);
	fd = open(filename, O_RDONLY);
	if (read(fd, &gpio_pin, sizeof(gpio_pin)) == -1)
		goto read_error;
	close(fd);
	
	if (board_start_acline(ntohl(gpio_pin)))
		fprintf(FPRINTF_FD, "board_init_channels error: no input pin found\n");
	else
		fprintf(FPRINTF_FD, "board_init_channels: input pin found - %02u\n", ntohl(gpio_pin));
	
	
	/* Read output channels uint32 */
	sprintf(filename, "%s/%s/%s", HAT_DIR, HAT_OUTPUTS_DIR, HAT_IO_CHANNELS);
	fd = open(filename, O_RDONLY);
	if (read(fd, &triac_status_len, sizeof(triac_status_len)) == -1)
		goto read_error;
	close(fd);
	
	/* fixup endianess */
	triac_status_len = ntohl(triac_status_len);
	
	/* Allocate memory for struct triac_status vector */
	triac = calloc(triac_status_len, sizeof(struct triac_status));
	if (triac == NULL)
		goto ptr_error;
	
	for (i = 0, channels = 0; i < triac_status_len; i++) {
		/* Read output channel N pin */
		sprintf(filename, "%s/%s/%u/%s", HAT_DIR, HAT_OUTPUTS_DIR, (i + 1), HAT_GPIO_PIN);
		fd = open(filename, O_RDONLY);
		if (read(fd, &gpio_pin, sizeof(gpio_pin)) == -1) {
			triac[i].gpio.status = error;
			close(fd);
		}
		else {
			close(fd);
			triac[i].gpio.pin = ntohl(gpio_pin);
			triac[i].phase.pos = 0;
			triac[i].phase.neg = 0;
			triac[i].phase.status = off;
			/* Read output channel N name */
			sprintf(filename, "%s/%s/%u/%s", HAT_DIR, HAT_OUTPUTS_DIR, (i + 1), HAT_GPIO_LABEL);
			fd = open(filename, O_RDONLY);
			if (read(fd, triac[i].gpio.label, sizeof(triac[i].gpio.label)) == -1)
				sprintf(triac[i].gpio.label, "nnn%u", i + 1);
			close(fd);
			
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
		

	fader_init(triac, triac_status_len);
	return channels;
	
	
read_error:
	fprintf(FPRINTF_FD, "board_init_channels read error: %d - %s\n", errno, strerror(errno));
	close(fd);
	return 0;
	
ptr_error:
	fprintf(FPRINTF_FD, "board_init_channels: memory error\n");
	free(triac);
	return 0;
}

/* Releases all channels, stops Kernel modules */
void board_free_channels(void)
{
	unsigned int i;
	
	for (i = 0; i < triac_status_len; i++) {
		if (triac[i].gpio.status == enabled) {
			fader_stop(i);
			board_stop_triacdrv(i + 1);
			triac[i].gpio.status = disabled;
			fprintf(FPRINTF_FD, "board_free_channels: channel %u released\n", i + 1);
		}
	}
	
	board_stop_acline();
	
	free(triac);
	
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
	
	for (i = 0; i < triac_status_len; i++) {
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
