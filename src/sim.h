#ifndef ISONIC_SIM_H
#define ISONIC_SIM_H

#include "circuit.h"

/* Resolves net values and evaluates all ICs. Safe to call every frame. */
void sim_step(Circuit *circuit);

#endif
