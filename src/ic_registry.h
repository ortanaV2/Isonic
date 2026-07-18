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
       pin_values[i] corresponds to pin_number == i + 1. PIN_POWER entries are left untouched. */
    void (*eval)(SignalValue *pin_values, int pin_count);
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
