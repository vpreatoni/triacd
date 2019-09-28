#include "fader.h"

/* Initializes fader pointers and variables */
void fader_init(struct triac_status *triac, unsigned int channels)
{
	unsigned int i;
	
	triac_fade_len = channels;
	
	/* Allocate memory for struct triac_status vector */
	fader = calloc(triac_fade_len, sizeof(struct triac_fade));
	if (fader == NULL)
		return;
	
	
	/* Struct fader will have a pointer to actual triac phase structure
	 * so fader can modify data pointed by struct triac_phase *
	 */
	for (i = 0; i < triac_fade_len; i++) {
		fader[i].status = STOPPED;
		fader[i].phase = &triac[i].phase;
	}
	
	return;
}

/* Fading thread launcher
 * Parses channel and fading values and launches thread
 * It also controls wether thread is running or not
 */
void fader_start(unsigned int i, unsigned int time, unsigned int pos_final, unsigned int neg_final)
{
	/* New fading request. Needs to cancel previous thread and start a new one */
	if (fader[i].status == STARTED) {
		fader[i].status = ABOUT_TO_STOP;
		pthread_cancel(fader[i].thread);
		fader[i].status = STOPPING;
		//JOIN OR DETACH??? How to catch thread_exit??. cancel or kill+signal?
		pthread_join(fader[i].thread, NULL);
		fader[i].status = STOPPED;
		fprintf(FPRINTF_FD, "fader_start: restarting fade thread\n");
	}
	
	fader[i].status = ABOUT_TO_START;
	fader[i].final_pos = pos_final;
	fader[i].final_neg = neg_final;
	fader[i].time = time;
	if (pthread_create(&fader[i].thread, NULL, fader_function, (void*)i))
		fprintf(FPRINTF_FD, "fader_start error: cannot start fader\n");
		
	return;
}

/* Fading thread stopper
 * Stops fading in case it's still running
 */
void fader_stop(unsigned int i)
{
	/* New fading request. Needs to cancel previous thread and start a new one */
	if (fader[i].status == STARTED) {
		fader[i].status = ABOUT_TO_STOP;
		pthread_cancel(fader[i].thread);
		fader[i].status = STOPPING;
		pthread_join(fader[i].thread, NULL);
		fader[i].status = STOPPED;
		fprintf(FPRINTF_FD, "fader_stop: fader stopped on channel %u\n", i + 1);
	}
	
	return;
}

bool float_cmp(float x, float y, float epsilon)
{
	if(fabs(x - y) < epsilon)
		return true;
	else 
		return false;
}

void * fader_function(void *_args)
{
	unsigned int i = (unsigned int)_args;
	/* Make backup of globals to work faster and avoid race conditions */
	unsigned int local_pos = fader[i].phase->pos;
	unsigned int local_neg = fader[i].phase->neg;
	
	unsigned int delay_step_ms = 50;
	unsigned int time_slots = fader[i].time / delay_step_ms;
	
	if (!time_slots) {
		fprintf(FPRINTF_FD, "fader_function error: cannot fade that fast!\n");
		pthread_exit(NULL);
	}
		
	int delta_pos = fader[i].final_pos - local_pos;
	int delta_neg = fader[i].final_neg - local_neg;
	
	float step_pos = (float)delta_pos / time_slots;
	float step_neg = (float)delta_neg / time_slots;
	
	float accum_pos = local_pos;
	float accum_neg = local_neg;
	
	fader[i].status = STARTED;
	fprintf(FPRINTF_FD, "fader_function: fader started on channel %u\n", i + 1);
// 	fprintf(FPRINTF_FD, "fader_function: requested final values %u\t%u\n", fader[i].final_pos, fader[i].final_neg);
// 	fprintf(FPRINTF_FD, "fader_function: %u steps of %u[ms]\n", time_slots, delay_step_ms);
// 	fprintf(FPRINTF_FD, "fader_function: deltas %i %i\n", delta_pos, delta_neg);
// 	fprintf(FPRINTF_FD, "fader_function: steps %f %f\n", step_pos, step_neg);

	while (!float_cmp(accum_pos, fader[i].final_pos, 1) || !float_cmp(accum_neg, fader[i].final_neg, 1)) {
		if (!float_cmp(accum_pos, fader[i].final_pos, 1)) {
			accum_pos += step_pos;
			fader[i].phase->pos = accum_pos;
			fader[i].phase->refresh = true;
		}
		if (!float_cmp(accum_neg, fader[i].final_neg, 1)) {
			accum_neg += step_neg;
			fader[i].phase->neg = accum_neg;
			fader[i].phase->refresh = true;
		}
		usleep(delay_step_ms * MSEC_TO_USEC);
	}

	fader[i].phase->pos = fader[i].final_pos;
	fader[i].phase->neg = fader[i].final_neg;
	fader[i].phase->refresh = true;
	
	fprintf(FPRINTF_FD, "fader_function: fader finished on channel %u\n", i + 1);
	fader[i].status = ABOUT_TO_STOP;
	pthread_detach(pthread_self());
	fader[i].status = STOPPED;
	pthread_exit(NULL);
}
