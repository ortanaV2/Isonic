#ifndef ISONIC_IC_REGISTRY_H
#define ISONIC_IC_REGISTRY_H

#include "types.h"

typedef struct {
    int pin_number;         /* 1-based, matches real datasheet numbering */
    const char *name;       /* e.g. "1A", "VCC" */
    PinDirection direction;
    int side;                /* 0 = left, 1 = right (DIP layout) */
} IC_PinDef;

typedef struct IC_Def {
    const char *name;             /* unique registry key, e.g. "SN7408" */
    int pin_count;
    const IC_PinDef *pins;        /* static array, length == pin_count, ordered by pin_number ascending starting at 1 */
    int width, height;             /* footprint size in grid cells */
    /* in-place eval: reads PIN_INPUT values, writes PIN_OUTPUT values.
       pin_values[i] corresponds to pin_number == i + 1. PIN_POWER entries are left untouched. */
    void (*eval)(SignalValue *pin_values, int pin_count);
} IC_Def;

#define IC_REGISTRY_MAX 64

void ic_registry_register(const IC_Def *def);
const IC_Def *ic_registry_get(const char *name);

#endif
