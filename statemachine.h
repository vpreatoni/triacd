#ifndef STATEMACHINE_H
#define STATEMACHINE_H

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

#define MODULE_DIR		"/sys/triacd"
/* Phase guard degrees
 * eg: how many degrees close to edge
 * values will be ignored
 */
#define PHASE_GUARD				5


void statem_loop(void);
int statem_send_command(char *, unsigned int, unsigned int);
void statem_set_off(unsigned int);
void statem_set_on(unsigned int);
void statem_set_sym(unsigned int, unsigned int);
void statem_set_asym(unsigned int, unsigned int, unsigned int);

#endif //STATEMACHINE_H
