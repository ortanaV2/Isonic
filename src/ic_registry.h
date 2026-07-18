#ifndef ISONIC_IC_REGISTRY_H
#define ISONIC_IC_REGISTRY_H

#include "types.h"

typedef struct {
    int pin_number;         /* 1-based, matches real datasheet numbering. The array holding these
                                must stay ordered by ascending pin_number starting at 1 - side and
                                position on the body are derived from it, not authored (see
                                ic_dip_body_size below), so this has to reflect the real physical
                                pinout for the body to come out correct. */
    const char *name;       /* e.g. "1A", "VCC" */
    PinDirection direction;
} IC_PinDef;

typedef struct IC_Def {
    const char *name;             /* unique registry key, e.g. "SN74HC08N" */
    int pin_count;
    const IC_PinDef *pins;        /* static array, length == pin_count, ordered by pin_number ascending starting at 1 */
    /* in-place eval: reads PIN_INPUT values, writes PIN_OUTPUT values.
       pin_values[i] corresponds to pin_number == i + 1. PIN_POWER entries are left untouched.
       Called several times per real simulation frame (see SIM_ITERATIONS in
       sim.c) so combinational signals can settle - never NULL. */
    void (*eval)(SignalValue *pin_values, int pin_count);
    /* Optional: called exactly once per real simulation frame (not once per
       eval() settle iteration - see sim.c's tick_clocked_ics), after this
       frame's combinational signals have settled, with this component
       instance's own persistent state (see IC_SEQ_STATE_BYTES in
       component.h). For ICs whose behavior depends on detecting a genuine
       clock edge or otherwise needs to remember something eval() can't (a
       counter must advance exactly once per real clock transition, not once
       per settle iteration). Leave NULL for purely combinational ICs -
       almost everything. */
    void (*clock_edge)(SignalValue *pin_values, int pin_count, unsigned char *state);
} IC_Def;

#define IC_REGISTRY_MAX 64

void ic_registry_register(const IC_Def *def);
const IC_Def *ic_registry_get(const char *name);

/* Standard DIP package footprint (grid cells) for a chip with this many pins -
   purely a function of pin_count, so every IC with the same pin count (e.g.
   two different DIP-14 parts) always renders with an identical body: fixed
   width, and a height driven by pins-per-side (pin_count / 2 pins on each
   side, split evenly, real-DIP-style - see component_init_ic). */
void ic_dip_body_size(int pin_count, int *out_w, int *out_h);

#endif
