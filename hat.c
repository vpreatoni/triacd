#include "hat.h"

/* Kernel module start and stop functions
 * system("modprobe") is preferred since it manages
 * dependencies
 */
int hat_start_modules(void)
{
	if (system("modprobe i2c_dev"))
		return EXIT_FAILURE;
	
	if (system("modprobe at24")) {
		system("rmmod i2c_dev");
		return EXIT_FAILURE;
	}
	
	return 0;
}


void hat_stop_modules(void)
{
	system("rmmod at24");
	system("rmmod i2c_dev");
	
	return;
}

/* Export eeprom address to sysfs filesystem
 * for easy access
 */
int hat_init_eeprom(void)
{
	int fd;
	char params[64];
	char filename[128];
	
	sprintf(params, "%s 0x%02X", EEPROM_TYPE, DEFAULT_EEPROM_ADDRESS);
	sprintf(filename, "%s%u/new_device", I2C_ADAPTER_FILE, I2C_BUS);
	fd = open(filename, O_WRONLY);
	if (write(fd, params, strlen(params)) <= 0) {
		fprintf(FPRINTF_FD, "hat_init_eeprom module error: %d - %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}
	close(fd);
	
	sprintf(filename, "%s%u/%u-%04X/eeprom", I2C_ADAPTER_FILE, I2C_BUS, I2C_BUS, DEFAULT_EEPROM_ADDRESS);
	if (access(filename, R_OK)) {
		fprintf(FPRINTF_FD, "hat_init_eeprom access error: %d - %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}
	
	return 0;
}

/* Delete sysfs node */
void hat_free_eeprom(void)
{
	int fd;
	char params[64];
	char filename[128];
	
	sprintf(params, "0x%02X", DEFAULT_EEPROM_ADDRESS);
	sprintf(filename, "%s%u/delete_device", I2C_ADAPTER_FILE, I2C_BUS);
	fd = open(filename, O_WRONLY);
	write(fd, params, strlen(params));
	close(fd);
	
	return;
}

/* Reads HAT EEPROM and fills struct hat_info
 * global variable.
 * EEPROM data structured can be seen on following project:
 * 
 * https://github.com/raspberrypi/hats
 * 
 */
int hat_read_eeprom(void)
{
	FILE *fp;
	char filename[128];
	
	struct header_t header;
	struct atom_t atom;
	struct vendor_info_d vinf;
	struct gpio_map_d gpiomap;
	uint16_t crc;
	unsigned int i,j;
	unsigned int out_cnt, in_cnt;
	
	if(hat_start_modules()) {
		fprintf(FPRINTF_FD, "hat_read_eeprom: cannot start kernel modules\n");
		goto modules_fail;
	}
	
	if(hat_init_eeprom()) {
		fprintf(FPRINTF_FD, "hat_read_eeprom: cannot init EEPROM\n");
		goto eeprom_fail;
	}
	
	sprintf(filename, "%s%u/%u-%04X/eeprom", I2C_ADAPTER_FILE, I2C_BUS, I2C_BUS, DEFAULT_EEPROM_ADDRESS);
	
	fp=fopen(filename, "r");
	if (!fp) {
		fprintf(FPRINTF_FD, "hat_read_eeprom: read error %s\n", filename);
		goto read_fail;
	}

	if (!fread(&header, sizeof(header), 1, fp)) goto data_fail;
	
	for (i = 0; i < MAX_PINS; i++) {
		hat_board.in_pins[i] = -1;
		hat_board.out_pins[i] = -1;
	}
	
	for (i = 0; i < header.numatoms; i++) {
		if (!fread(&atom, ATOM_SIZE-CRC_SIZE, 1, fp)) goto data_fail;
				
		long pos = ftell(fp);
		char *atom_data = (char *) malloc(atom.dlen + ATOM_SIZE-CRC_SIZE);
		memcpy(atom_data, &atom, ATOM_SIZE-CRC_SIZE);
		if (!fread(atom_data+ATOM_SIZE-CRC_SIZE, atom.dlen, 1, fp)) goto data_fail;
		uint16_t calc_crc = getcrc(atom_data, atom.dlen-CRC_SIZE+ATOM_SIZE-CRC_SIZE);
		fseek(fp, pos, SEEK_SET);
		
		if (atom.type == ATOM_VENDOR_TYPE) {
			//decode vendor info
			if (!fread(&vinf, VENDOR_SIZE, 1, fp)) goto data_fail;

			hat_board.version = vinf.pver;
			
			vinf.vstr = (char *) malloc(vinf.vslen+1);
			vinf.pstr = (char *) malloc(vinf.pslen+1);
			if (!fread(vinf.vstr, vinf.vslen, 1, fp)) goto data_fail;
			if (!fread(vinf.pstr, vinf.pslen, 1, fp)) goto data_fail;
			//close strings
			vinf.vstr[vinf.vslen] = 0;
			vinf.pstr[vinf.pslen] = 0;
			
			hat_board.vendor = vinf.vstr;
			hat_board.product = vinf.pstr;
			
			if (!fread(&crc, CRC_SIZE, 1, fp)) goto data_fail;
			
		} else if (atom.type == ATOM_GPIO_TYPE) {
			//decode GPIO map
			if (!fread(&gpiomap, GPIO_SIZE, 1, fp)) goto data_fail;
			
			for (j = 0, in_cnt = 0, out_cnt = 0; j < GPIO_COUNT && in_cnt < MAX_PINS && out_cnt < MAX_PINS; j++) {
				if (gpiomap.pins[j] & (1<<7)) {
					//board uses this pin
					switch ((gpiomap.pins[j] & 7)) { //111
						case 0:
							hat_board.in_pins[in_cnt] = j;
							in_cnt++;
							break;
						case 1:
							hat_board.out_pins[out_cnt] = j;
							out_cnt++;
							break;
					}
				}
			}
			
			if (!fread(&crc, CRC_SIZE, 1, fp)) goto data_fail;
			
		} else {
			fprintf(FPRINTF_FD, "hat_read_eeprom: unrecognised atom type\n");
			goto data_fail;
		}
		
		if (calc_crc != crc)
			fprintf(FPRINTF_FD, "hat_read_eeprom: atom CRC16 mismatch - 0x%02X", crc);
	}

	fclose(fp);
	hat_free_eeprom();
	hat_stop_modules();
	return 0;
	
data_fail:
	fprintf(FPRINTF_FD, "hat_read_eeprom: unexpected EOF or error occurred\n");
	fclose(fp);
read_fail:
	hat_free_eeprom();
eeprom_fail:
	hat_stop_modules();
modules_fail:
	return EXIT_FAILURE;
}

/* Returns next input pin available
 * Returns -1 when no more pins available
 */
int hat_get_next_in(void)
{
	static unsigned int i = 0;
	int retval;
	
	if (i < MAX_PINS) {
		retval = hat_board.in_pins[i];
		i++;
	}
	else
		retval = -1;
	
	return retval;
}

/* Returns next output pin available
 * Returns -1 when no more pins available
 */
int hat_get_next_out(void)
{
	static unsigned int i = 0;
	int retval;
	
	if (i < MAX_PINS) {
		retval = hat_board.out_pins[i];
		i++;
	}
	else
		retval = -1;
	
	return retval;
}
