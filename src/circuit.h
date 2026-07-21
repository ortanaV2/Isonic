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
    int layer_slot;   /* index into Circuit.layers[] - which copper layer this wire is routed on, see MAX_LAYERS below */
} Wire;

typedef struct {
    int x, y;
} GridPoint;

/* LAYER_ROLE_GND/POWER layers are modeled as a real continuous plane, not
   routed traces: every wire on such a layer is unioned into one net
   together regardless of position (see circuit_rebuild_nets), and forces
   that net LOW/HIGH respectively (see sim.c's gather_drivers) - "tapping
   in" anywhere on the layer is enough, no routed path between two taps is
   needed, same as a real ground/power pour. Exactly one layer holds each
   of these two roles at a time, and neither can be deleted (see
   circuit_remove_layer) - LAYER_ROLE_NORMAL layers behave like an ordinary
   routed copper layer (TOP-Signal/BOTTOM-Signal by default, or any
   user-added one). */
typedef enum { LAYER_ROLE_NORMAL, LAYER_ROLE_GND, LAYER_ROLE_POWER } LayerRole;

/* 9 to fit cleanly in the 1-9 number-key range used to pick the active
   routing layer (see input_handler.c) - one keypress always maps to
   exactly one digit. */
#define MAX_LAYERS 9
#define MAX_VIAS 512 /* same scale as MAX_WIRES */

typedef struct {
    int in_use;
    char name[24];
    /* plain RGB, not SDL_Color - circuit.h stays render-agnostic, same
       reasoning as SignalValue/PinDirection living in types.h rather than
       anywhere SDL-aware. render.c turns this into an SDL_Color itself. */
    unsigned char color_r, color_g, color_b;
    LayerRole role;
} Layer;

/* A via bridges exactly two specific layers at one grid point - real vias
   can be blind/buried between a specific layer pair, not automatically
   "every layer at once". Two wires on different layers whose endpoints
   touch (exact coincidence or landing mid-span on one another) get an
   automatic via placed between exactly their two layers (see
   circuit_add_wire) - manually placing one instead (TOOL_VIA) requires
   picking the two layers explicitly, since there's no touching wire pair
   to infer them from. */
typedef struct {
    int in_use;
    int x, y;
    int layer_slot_a, layer_slot_b;
} Via;

/* 32/64 - schematic annotations, not electrical structure, so there's no
   reason for these to scale with MAX_COMPONENTS/MAX_WIRES the way vias do;
   plenty of headroom for organizing even a large schematic into sections. */
#define MAX_SECTIONS 32
#define MAX_TEXT_LABELS 64
#define SECTION_LABEL_MAX_LEN 32
#define TEXT_LABEL_MAX_LEN 48
/* Smallest a section's rectangle may ever be (grid cells, both axes) - keeps
   a degenerate zero/negative-size drag or corner-handle drag from ever
   collapsing it into something invisible/unselectable. */
#define SECTION_MIN_SIZE 1

/* A purely visual grouping rectangle - "Section-Labeling" - with no
   electrical meaning at all (no pins, doesn't participate in
   circuit_rebuild_nets, never saved/loaded as anything but its own
   geometry+text). x0/y0/x1/y1 are grid corners, always kept normalized
   (x0<=x1, y0<=y1) by circuit_add_section/circuit_set_section_rect below -
   callers never need to sort them themselves, e.g. after dragging a corner
   handle past the opposite one. locked freezes it against move/resize/
   rename (see input_handler.c) - deliberately NOT against selection or
   deletion, same as most design tools' own "lock" semantics... except
   Isonic's delete_selection DOES also respect it (see its own comment) -
   locking is meant to mean "leave this alone", and an accidental Delete
   while multi-selecting nearby components is exactly the kind of accident
   it exists to prevent. */
typedef struct {
    int in_use;
    int x0, y0, x1, y1;
    char label[SECTION_LABEL_MAX_LEN + 1];
    int locked;
    int selected;
} Section;

/* A single freestanding line of text placed on the canvas - "just a label",
   no rectangle, no lock, nothing electrical. (x,y) is the grid point its
   text grows right and down from (matching how every other on-canvas label
   in this app anchors from a top-left-ish point). */
typedef struct {
    int in_use;
    int x, y;
    char text[TEXT_LABEL_MAX_LEN + 1];
    int selected;
} TextLabel;

typedef struct {
    Component components[MAX_COMPONENTS];
    int component_high_water;

    Wire wires[MAX_WIRES];
    int wire_high_water;
    SignalValue wire_values[MAX_WIRES]; /* resolved net value per wire, filled by sim_step */

    Via vias[MAX_VIAS];
    int via_high_water;

    Section sections[MAX_SECTIONS];
    int section_high_water;

    TextLabel text_labels[MAX_TEXT_LABELS];
    int text_label_high_water;

    /* layers[] is a fixed-slot table (like components/wires above) - a
       layer's slot index is its stable identity, referenced by
       Wire.layer_slot and Via.layer_slot_a/b, and never reassigned by
       reordering. layer_order[] holds slot indices in current
       display/stack order: position i (0-based) is "layer #(i+1)" in the
       Layers panel and the number-key binding, and position 0 /
       layer_order_count-1 are the two outer layers component pins bridge
       to (see circuit_rebuild_nets) - reordering the stack changes both
       of those without ever touching which wires reference which layer. */
    Layer layers[MAX_LAYERS];
    int layer_order[MAX_LAYERS];
    int layer_order_count;

    int pin_net[TOTAL_POINTS]; /* union-find parent array over pins + wire endpoints */

    GridPoint junctions[MAX_JUNCTIONS]; /* cached connection points, for rendering only */
    int junction_count;
} Circuit;

/* Seeds the four default layers (TOP-Signal/yellow, GND/blue, +5V/orange,
   BOTTOM-Signal/violet, in that stack order) alongside the usual empty
   circuit. */
void circuit_init(Circuit *circuit);

int circuit_add_ic(Circuit *circuit, int grid_x, int grid_y, const IC_Def *def);
void circuit_remove_component(Circuit *circuit, int component_id);

/* Returns the id of the first wire segment added, or -1 if from == to. Rebuilds
   nets. For INPUT/OUTPUT kind, from/to is the terminal/open end respectively
   (see Wire above). If this wire crosses an existing wire's endpoint, or an
   existing wire crosses this one's, both are split into independent segments
   at that point instead of merely tapping into an unbroken run - so every
   segment stays separately selectable and deletable - but only if the
   existing wire is on the SAME layer_slot; a different-layer touch instead
   triggers an automatic via there bridging the two layers (see Via above),
   never a silent geometric merge. */
int circuit_add_wire(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind, int layer_slot);
/* No splitting/auto-via inference, no rebuild - for schematic_load, which
   restores an already fully-resolved wire list verbatim rather than
   replaying it through the interactive-drawing heuristics above. See its
   own comment in circuit.c. */
int circuit_add_wire_raw(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind, int layer_slot);
void circuit_remove_wire(Circuit *circuit, int wire_id);

/* Adds a via bridging layer_slot_a/b at (x,y) - no-ops (returns -1) if one
   already bridges that exact pair there, so circuit_add_wire's auto-via
   creation can call this unconditionally without double-placing. */
int circuit_add_via(Circuit *circuit, int x, int y, int layer_slot_a, int layer_slot_b);
/* No rebuild - same schematic_load use case as circuit_add_wire_raw above. */
int circuit_add_via_raw(Circuit *circuit, int x, int y, int layer_slot_a, int layer_slot_b);
void circuit_remove_via(Circuit *circuit, int via_id);
int circuit_find_via_at(const Circuit *circuit, int x, int y);

/* Layer management - layer_order_pos is a position in layer_order[]
   (0-based; "layer #N" in the UI is position N-1), not a raw layers[]
   slot index. */
int circuit_add_layer(Circuit *circuit, const char *name, unsigned char r, unsigned char g, unsigned char b);
/* Returns 0 (and leaves the layer alone) if it's GND/+5V or still has any
   wire on it, 1 if it was removed. */
int circuit_remove_layer(Circuit *circuit, int layer_order_pos);
/* direction: -1 moves it earlier (up), +1 later (down); no-op at either end. */
void circuit_move_layer(Circuit *circuit, int layer_order_pos, int direction);
/* True if any in-use wire currently references this layers[] slot - what
   circuit_remove_layer itself checks, also useful for the panel to grey
   out a delete control before the user even tries. */
int circuit_layer_in_use(const Circuit *circuit, int layer_slot);

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

/* Nearest wire endpoint to (fx,fy) within tolerance (grid units), written to
   out_x/out_y - the "snap to a node" test shared by Select-mode node-
   dragging and TOOL_VIA's node-snap placement/ghost preview (see
   input_handler.c/app.c). Returns 0 if none found within tolerance. */
int circuit_find_wire_node_near(const Circuit *circuit, float fx, float fy, float tolerance, int *out_x, int *out_y);

/* The layer_slot of any in-use wire with an end at exactly (x,y), or -1 if
   none - used to find a node's own layer for TOOL_VIA (see
   circuit_find_wire_node_near above). If several wires meet there, any one
   of them is an equally valid representative of "this node". */
int circuit_wire_layer_at_point(const Circuit *circuit, int x, int y);

/* True if a component footprint of size (w,h) at (x,y) would overlap an existing
   component (other than ignore_id). Used to keep placements from stacking. */
int circuit_footprint_overlaps(const Circuit *circuit, int x, int y, int w, int h, int ignore_id);

/* Adds a Section, normalizing the two corners itself (either diagonal works)
   and clamping to SECTION_MIN_SIZE - so callers never need to sort/validate
   a just-dragged rectangle themselves. Returns -1 if the table's full. */
int circuit_add_section(Circuit *circuit, int x0, int y0, int x1, int y1, const char *label);
void circuit_remove_section(Circuit *circuit, int id);
/* Re-normalizes/clamps the same way circuit_add_section does - used both
   when whole-section dragging (all four corners shift by the same delta, so
   this is overkill but harmless there) and when a single corner handle is
   dragged (where it actually matters, including the case where that corner
   gets dragged past its opposite one). */
void circuit_set_section_rect(Circuit *circuit, int id, int x0, int y0, int x1, int y1);
/* Topmost (highest id - i.e. most recently added) in-use section whose
   RECTANGLE OUTLINE (not its filled interior - see the .c file) passes
   within tolerance of (fx,fy), or -1. A corner is just where two border
   segments meet, so this alone covers hitting a corner point too, without a
   separate check. (fx,fy) is the cursor in float grid space (unrounded),
   same convention as circuit_find_wire_at. Its label/lock-button/corner-
   handles are screen-space-only widgets with no grid-space equivalent
   (font-dependent sizing), so they're hit-tested directly in
   input_handler.c against render.c's shared bounds helpers instead of
   through here. */
int circuit_find_section_at(const Circuit *circuit, float fx, float fy, float tolerance);

int circuit_add_text_label(Circuit *circuit, int x, int y, const char *text);
void circuit_remove_text_label(Circuit *circuit, int id);

#endif
