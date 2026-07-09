#ifndef ISONIC_COMPONENT_H
#define ISONIC_COMPONENT_H

#include "types.h"

typedef struct IC_Def IC_Def; /* forward declaration, defined in ic_registry.h */

#define MAX_PINS_PER_COMPONENT 16

/* Input/Output are no longer components - they're just wires with a WIRE_KIND_INPUT/
   OUTPUT tag (see circuit.h) so they can be drawn/dragged exactly like any wire. */
typedef enum {
    COMP_IC
} ComponentType;

typedef struct {
    const char *name;
    PinDirection direction;
    int local_dx, local_dy; /* offset from component origin, in grid cells */
    SignalValue value;       /* resolved net value, cached each sim step */
} Pin;

typedef struct {
    int in_use;
    ComponentType type;
    int grid_x, grid_y;      /* origin in grid cells */
    const IC_Def *ic_def;     /* IC definition this instance was placed from */
    Pin pins[MAX_PINS_PER_COMPONENT];
    int pin_count;
    int selected;
} Component;

/* Initializes an already-allocated component slot for the given IC. */
void component_init_ic(Component *c, int grid_x, int grid_y, const IC_Def *def);

/* Absolute grid position of a pin (component origin + local offset). */
void component_pin_world_pos(const Component *c, int pin_index, int *out_x, int *out_y);

/* Returns pin index whose absolute position matches (x,y), or -1 if none. */
int component_find_pin_at(const Component *c, int x, int y);

/* Footprint size in grid cells (ic_def->width/height). */
void component_get_size(const Component *c, int *out_w, int *out_h);

#endif
