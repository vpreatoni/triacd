#include "statemachine.h"


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
	close(fd);
	
	return 0;
}

void statem_set_off(unsigned int i)
{
	triac[i].phase.status = off;
	statem_send_command(triac[i].gpio.label, 0, 0);
	
	return;
}

void statem_set_on(unsigned int i)
{

	triac[i].phase.status = on;
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
	
	/* Do some adjustments to avoid passing near-edge values
	 * eg: 179 2	1	177 	and so
	 */
	if (pos <= (0 + PHASE_GUARD))
		pos = 0;
	else if (pos >= (180 - PHASE_GUARD))
		pos = 180;
	
	if (neg <= (0 + PHASE_GUARD))
		neg = 0;
	else if (neg >= (180 - PHASE_GUARD))
		neg = 180;

	
	statem_send_command(triac[i].gpio.label, pos, neg);
	
	return;
}

void statem_loop(void)
{
	unsigned int i;
	unsigned int local_pos, local_neg;
	
	///// TRIAC parameter update and basic on/off functions
	for (i = 0; i < MAX_TRIACS; i++) {
		if (triac[i].gpio.status == enabled) {
			/* Make backup of globals to work faster and avoid race conditions */
			local_pos = triac[i].phase.pos;
			local_neg = triac[i].phase.neg;
			
			//if (triac[i].fader.status = ABOUT_TO_START) //FADER REQUESTED, LAUNCH THREAD. Thread will update pos and neg only. Statemachine will send commands so no matter if fader works very fast.
			

			switch (triac[i].phase.status) {
				case off:
					if (local_pos >= (180 - PHASE_GUARD) && local_neg >= (180 - PHASE_GUARD)) {
						statem_set_on(i);
						break;
					}
					
					if (local_pos < (180 - PHASE_GUARD) && local_pos > (0 + PHASE_GUARD)) {
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
					if (local_pos <= (0 + PHASE_GUARD) && local_neg <= (0 + PHASE_GUARD)) { 
						statem_set_off(i);
						break;
					}
					
					if (local_pos < (180 - PHASE_GUARD) && local_pos > (0 + PHASE_GUARD)) {
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
					if (local_pos <= (0 + PHASE_GUARD) && local_neg <= (0 + PHASE_GUARD)) { 
						statem_set_off(i);
						break;
					}
					
					if (local_pos >= (180 - PHASE_GUARD) && local_neg >= (180 - PHASE_GUARD)) {
						statem_set_on(i);
						break;
					}
					
					if (local_pos != local_neg) {
						statem_set_asym(i, local_pos, local_neg);
						break;
					}
					
					/* no state change */
					break;
					
				case asym:
					if (local_pos <= (0 + PHASE_GUARD) && local_neg <= (0 + PHASE_GUARD)) { 
						statem_set_off(i);
						break;
					}
					
					if (local_pos >= (180 - PHASE_GUARD) && local_neg >= (180 - PHASE_GUARD)) {
						statem_set_on(i);
						break;
					}

					if (local_pos < (180 - PHASE_GUARD) && local_pos > (0 + PHASE_GUARD) && local_neg == local_pos) {
						statem_set_sym(i, local_pos);
						break;
					}
					
					/* no state change */
					break;
					
				default:
					break;
			} //end switch
		} //end if enabled
	} //end for
	
	return;
}
