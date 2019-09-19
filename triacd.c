/*
 * triacd.c - TRIAC driver daemon
 * Used with systemd, it creates a service daemon to control OpenIndoor
 * Opto-TRIAC board.
 * 
 * It can also run on stand-alone mode, or you can pass command line arguments
 * to control triacd daemon.
 * 
 * triacd will launch required kernel modules (aclinedrv.ko and triacNdrv.ko)
 * if it is required by state machine.
 * Eg: if TRIAC is set to fully on or fully off, no kernel module is necesary
 * since TRIAC can be controlled by a single GPIO pulse.
 * 
 * Instead, if a phase control mechanism is requested, triacd will launch
 * a triacNdrv.ko instance due to strict timing requirements.
 *
 * Copyright (C) 2019 Victor Preatoni
 */

#include "triacd.h"


/* SIGINT and SIGTERM catcher */
void term(int signum)
{
	daemon_stop = 1;
}

void triacd_print_params(char *argv)
{
	fprintf(FPRINTF_FD, "Usage:\n");
	fprintf(FPRINTF_FD, "No parameter\tto start triacd daemon\n");
	fprintf(FPRINTF_FD, "-c [1-4]\tto select TRIAC channel\n");
	fprintf(FPRINTF_FD, "-f\t\tto start fade-in or fade-out\n");
	fprintf(FPRINTF_FD, "-t [msec]\tto define fade-in or fade-out time\n");
	fprintf(FPRINTF_FD, "\t\t* Fader requires a fade-time. If no conduction angle passed, fader will fade out to zero\n");
	fprintf(FPRINTF_FD, "-p [0-180]\tto define positive phase conduction degrees\n");
	fprintf(FPRINTF_FD, "-n [0-180]\tto define negative phase conduction degrees\n");
	fprintf(FPRINTF_FD, "\t\t* If no negative angle passed, TRIAC will work on symmetric phase mode\n");
	fprintf(FPRINTF_FD, "\t\t* If no negative OR positive angle passed, TRIAC will turn off\n");
	fprintf(FPRINTF_FD, "\nEg: %s -c4 -f -t5000 -p110\tto start fading channel 4 for 5sec up to 110deg\n", argv);
	fprintf(FPRINTF_FD, "    %s -c1 -p110 -n30\t\tto set channel 1 to 110deg positive / 30deg negative\n", argv);
	fprintf(FPRINTF_FD, "    %s -c2\t\t\tto turn off channel 2\n", argv);
	fprintf(FPRINTF_FD, "    %s -c3 -t3000\t\t\tto turn off channel 3 after 3sec\n", argv);
	fprintf(FPRINTF_FD, "    %s -c1 -t20000 -p180\t\tto fully turn on channel 1 after 20sec\n", argv);
	return;
}

/* Command line parser
 * Will launch daemon or single run mode
 * depending if any parameter is passed or not
 */
int main(int argc, char *argv[])
{
	bool fade_request = false;
	int time = 0;
	int pos_phase = 0;
	int neg_phase = 0;
	int channel = 0;
	int opt;
	int exit_state;
	
	if (argc > 1) {
		while ((opt = getopt(argc, argv, "c:ft:p:n:")) != -1) {
			switch (opt) {
				case 'c':
					channel = atoi(optarg);
					break;
				case 'f':
					fade_request = true;
					break;
				case 't':
					time = atoi(optarg);
					break;
				case 'p':
					pos_phase = atoi(optarg);
					neg_phase = pos_phase;
					break;
				case 'n':
					neg_phase = atoi(optarg);
					break;
				default:
					triacd_print_params(argv[0]);
					exit(EXIT_FAILURE);
			}
		}
		exit_state = triacd_set_params(channel, fade_request, time, pos_phase, neg_phase);
	}
	else
		exit_state = triacd_main_loop();
	
	exit(exit_state);
}

/* Single-run parameter sanity-check and Message Queue sender */
int triacd_set_params(int channel, bool fade, int time, int pos, int neg)
{
	mqd_t mq;
	union msg_q packed_data;
	
	if (channel == 0) {
		fprintf(FPRINTF_FD, "Must define channel: -c [1-%u]\n", MAX_TRIACS);
		return EXIT_FAILURE;
	}
	
	if (fade && time == 0) {
		fprintf(FPRINTF_FD, "Must define fade time: -t [msec]\n");
		return EXIT_FAILURE;
	}
	
	if (channel < 0 || time < 0 || pos < 0 || neg < 0) {
		fprintf(FPRINTF_FD, "Cannot use negative values!\n");
		return EXIT_FAILURE;
	}
	
	if (channel > MAX_TRIACS) {
		fprintf(FPRINTF_FD, "Maximum is %u channels\n", MAX_TRIACS);
		return EXIT_FAILURE;
	}
	
	if (pos > 180 || neg > 180) {
		fprintf(FPRINTF_FD, "Conduction angle limit is 180deg\n");
		return EXIT_FAILURE;
	}
	
	/* open the message queue only if it was previosly created by daemon*/
	mq = mq_open(QUEUE_NAME, O_WRONLY | O_NONBLOCK);
	if (mq == (mqd_t) -1) {
		fprintf(FPRINTF_FD, "Message queue error: %d - %s\nIs daemon running?...\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}
	
	packed_data.triac.channel = (unsigned int)channel;
	packed_data.triac.fade = fade;
	packed_data.triac.time = (unsigned int)time;
	packed_data.triac.pos = (unsigned int)pos;
	packed_data.triac.neg = (unsigned int)neg;
	
	
	if ((mq_send(mq, packed_data.message, sizeof(struct triac_data), 0)) < 0) {
		fprintf(FPRINTF_FD, "Message queue error: %d - %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}
	
	mq_close(mq);
	
	return EXIT_SUCCESS;
}

/* Daemon parameter proccessing that updates Global struct triac_status
 * Note: no mutex needed, since most variables are defined as atomic_unit
 */
void triacd_refresh_params(struct triac_data triac_params)
{
	unsigned int i;
	
	i = triac_params.channel - 1;
	if (i < max_channels)
		if (triac[i].gpio.status == enabled) {
			triac[i].phase.pos = triac_params.pos;
			triac[i].phase.neg = triac_params.neg;
			if (triac_params.fade) {
				triac[i].fader.remaining_time = triac_params.time;
				if (triac[i].fader.status != STARTED)
					triac[i].fader.status = ABOUT_TO_START;
			}
			fprintf(FPRINTF_FD, "%s request:\n", triac[i].gpio.label);
			if (triac_params.fade)
				fprintf(FPRINTF_FD, "\tfading time %ums\n", triac_params.time);
			fprintf(FPRINTF_FD, "\tpos phase %udeg\n", triac_params.pos);
			fprintf(FPRINTF_FD, "\tneg phase %udeg\n", triac_params.neg);
		}
	
	return;
}

/* Daemon message queue initializer */
mqd_t triacd_init_mq(void)
{
	mqd_t mq;
	struct mq_attr attr;
	
	/* initialize the queue attributes */
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(struct triac_data);
	
	/* create the message queue */
	mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0644, &attr);
	
	return mq;
}

/* Daemon message queue initializer */
void triacd_end_mq(mqd_t mq)
{
	mq_close(mq);
	mq_unlink(QUEUE_NAME);
	
	return;
}

/* Signal handler installer */
void triacd_init_signals(void)
{
	struct sigaction action;
	
	/* install signal handlers */
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	
	return;
}
	

/* Daemon mode main-loop */
int triacd_main_loop(void)
{
	mqd_t mq;
	union msg_q packed_data;
	
	
	fprintf(FPRINTF_FD, "Starting main loop...\n");
	
	mq = triacd_init_mq();
	if (mq == (mqd_t) -1) {
		fprintf(FPRINTF_FD, "Message queue error: %d - %s\n", errno, strerror(errno));
		return(EXIT_FAILURE);
	}
	
	triacd_init_signals();
	
	/* Get Opto-TRIAC board available channels and init them*/
	max_channels = board_init_channels();
	if (max_channels)
		fprintf(FPRINTF_FD, "%u channels configured\n", max_channels);
	else
		fprintf(FPRINTF_FD, "Error: no channels configured. Is EEPROM valid?\n");
// 	statem_init_threads(); necesario? o mudarlo a un fader.c q tenga toda la logica del fader en si.
	
	/* main loop */
	while (!daemon_stop) {
		if (mq_receive(mq, packed_data.message, sizeof(struct triac_data), NULL))
			triacd_refresh_params(packed_data.triac);
		
		/* actualiza estados de acuerdo a los parametros (sym, asym, on, off, fading) */
		statem_loop();
		
		usleep(THREAD_LATENCY);
	}
	
	fprintf(FPRINTF_FD, "Stopping...\n");
	triacd_end_mq(mq);
	board_free_channels();
	return (EXIT_SUCCESS);
}


