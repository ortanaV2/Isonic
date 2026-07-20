#ifndef ISONIC_COMPONENT_H
#define ISONIC_COMPONENT_H

#include "types.h"

typedef struct IC_Def IC_Def; /* forward declaration, defined in ic_registry.h */

/* 28 to fit the widest DIP package currently modeled (AT28C64B-15PU, a
   28-pin EEPROM) - every array/loop bound elsewhere derives from this macro
   rather than hardcoding a pin count, so raising it is sufficient on its
   own. */
#define MAX_PINS_PER_COMPONENT 28

/* Scratch space for ICs with an IC_Def.clock_edge callback (see
   ic_registry.h) to remember something across real simulation frames that
   eval()'s "read back your own previous output" trick can't capture - e.g. a
   counter's running count, and the clock level last seen (to detect an edge
   at all). 4 bytes is enough for a 16-bit counter plus a level byte; bump if
   a future sequential IC needs more. Zeroed for free by component_init_ic's
   memset - no separate init needed. */
#define IC_SEQ_STATE_BYTES 4

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
    int decorative;          /* see IC_PinDef.decorative (ic_registry.h) - copied in component_init_ic */
} Pin;

typedef struct {
    int in_use;
    ComponentType type;
    int grid_x, grid_y;      /* origin in grid cells - always the top-left corner of the CURRENT
                                (rotation-applied) footprint, see component_get_size/component_pin_world_pos */
    const IC_Def *ic_def;     /* IC definition this instance was placed from */
    Pin pins[MAX_PINS_PER_COMPONENT];
    int pin_count;
    int selected;
    /* 0-3, quarter turns counterclockwise from the IC's natural (pin-1-
       top-left) orientation - see component_pin_world_pos for the rotation
       math and app.h's place_rotation for how this gets set while placing. */
    int rotation;
    unsigned char seq_state[IC_SEQ_STATE_BYTES]; /* see IC_SEQ_STATE_BYTES above */
} Component;

/* Initializes an already-allocated component slot for the given IC, at
   rotation 0 - see circuit_add_ic for setting a placed instance's rotation. */
void component_init_ic(Component *c, int grid_x, int grid_y, const IC_Def *def);

/* Absolute grid position of a pin (component origin + local offset, rotated
   by c->rotation quarter-turns - see the .c file). */
void component_pin_world_pos(const Component *c, int pin_index, int *out_x, int *out_y);

/* Returns pin index whose absolute position matches (x,y), or -1 if none. */
int component_find_pin_at(const Component *c, int x, int y);

/* Footprint size in grid cells - see ic_dip_body_size in ic_registry.h. Width
   and height are swapped from the IC's natural size at rotation 1/3 (90/270
   degrees). */
void component_get_size(const Component *c, int *out_w, int *out_h);

#endif
