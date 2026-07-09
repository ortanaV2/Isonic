#ifndef ISONIC_CIRCUIT_H
#define ISONIC_CIRCUIT_H

#include "component.h"
#include "ic_registry.h"

#define MAX_COMPONENTS 256
#define MAX_WIRES 512
#define MAX_JUNCTIONS 1024

#define MAX_GLOBAL_PINS (MAX_COMPONENTS * MAX_PINS_PER_COMPONENT)
#define GLOBAL_PIN_ID(component_id, pin_index) ((component_id) * MAX_PINS_PER_COMPONENT + (pin_index))

/* Wire endpoints live past the pin id range in the same union-find array. */
#define WIRE_POINT_ID(wire_id, end) (MAX_GLOBAL_PINS + (wire_id) * 2 + (end))
#define TOTAL_POINTS (MAX_GLOBAL_PINS + MAX_WIRES * 2)

/* WIRE_KIND_NORMAL is an ordinary wire. WIRE_KIND_INPUT/OUTPUT are still just
   wires (same struct, same connectivity, same drag-to-draw placement) - the
   only difference is the "from" end renders as a clickable H/L terminal
   instead of a plain connection dot, and INPUT wires drive their own value
   onto the net instead of just reading it (see sim.c / render.c). */
typedef enum { WIRE_KIND_NORMAL, WIRE_KIND_INPUT, WIRE_KIND_OUTPUT } WireKind;

/* A wire is a free straight segment between two grid points. It does not
   reference any component/pin - connectivity is derived purely from where its
   endpoints land relative to other wires and pin tips (see circuit_rebuild_nets). */
typedef struct {
    int in_use;
    int from_x, from_y; /* the H/L terminal end, for INPUT/OUTPUT kind */
    int to_x, to_y;       /* the open end - connects into the rest of the circuit like any wire endpoint */
    int selected;
    WireKind kind;
    int input_value; /* only meaningful when kind == WIRE_KIND_INPUT; user-toggled */
} Wire;

typedef struct {
    int x, y;
} GridPoint;

typedef struct {
    Component components[MAX_COMPONENTS];
    int component_high_water;

    Wire wires[MAX_WIRES];
    int wire_high_water;
    SignalValue wire_values[MAX_WIRES]; /* resolved net value per wire, filled by sim_step */

    int pin_net[TOTAL_POINTS]; /* union-find parent array over pins + wire endpoints */

    GridPoint junctions[MAX_JUNCTIONS]; /* cached connection points, for rendering only */
    int junction_count;
} Circuit;

void circuit_init(Circuit *circuit);

int circuit_add_ic(Circuit *circuit, int grid_x, int grid_y, const IC_Def *def);
void circuit_remove_component(Circuit *circuit, int component_id);

/* Returns the new wire id, or -1 if from == to. Rebuilds nets. For INPUT/OUTPUT
   kind, from/to is the terminal/open end respectively (see Wire above). */
int circuit_add_wire(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind);
void circuit_remove_wire(Circuit *circuit, int wire_id);

void circuit_rebuild_nets(Circuit *circuit);
int circuit_pin_net_root(Circuit *circuit, int component_id, int pin_index);
int circuit_wire_net_root(Circuit *circuit, int wire_id);

/* Number of pin tips + wire endpoints that land exactly on (x, y) - does NOT
   count wires merely passing through (see circuit->junctions for those). A
   dot should only ever be drawn where this is 1 (a lone, unconnected pin or
   dangling wire end) - anywhere else, either nothing should be drawn (a plain
   1-to-1 connection, count == 2) or the point is already a cached junction. */
int circuit_point_connection_count(const Circuit *circuit, int x, int y);

/* True if (x, y) is a cached real junction (3+ endpoints, or a mid-span tap). */
int circuit_point_is_junction(const Circuit *circuit, int x, int y);

/* Hit-testing, in grid coordinates. Returns -1 / 0 if nothing found. */
int circuit_find_component_at(const Circuit *circuit, int x, int y);
int circuit_find_pin_at(const Circuit *circuit, int x, int y, int *out_component_id, int *out_pin_index);
/* (fx,fy) is the cursor in float grid space (unrounded); tolerance is in grid
   units. Finds the closest wire within tolerance of the point-to-segment distance. */
int circuit_find_wire_at(const Circuit *circuit, float fx, float fy, float tolerance);

/* True if a component footprint of size (w,h) at (x,y) would overlap an existing
   component (other than ignore_id). Used to keep placements from stacking. */
int circuit_footprint_overlaps(const Circuit *circuit, int x, int y, int w, int h, int ignore_id);

#endif
