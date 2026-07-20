#include <string.h>
#include "component.h"
#include "ic_registry.h"

static void clear_component(Component *c) {
    int in_use = c->in_use;
    memset(c, 0, sizeof(Component));
    c->in_use = in_use;
}

void component_init_ic(Component *c, int grid_x, int grid_y, const IC_Def *def) {
    clear_component(c);
    c->type = COMP_IC;
    c->grid_x = grid_x;
    c->grid_y = grid_y;
    c->ic_def = def;
    c->pin_count = def->pin_count;
    if (c->pin_count > MAX_PINS_PER_COMPONENT) {
        c->pin_count = MAX_PINS_PER_COMPONENT;
    }

    int body_w, body_h;
    ic_dip_body_size(def->pin_count, &body_w, &body_h);
    (void)body_h; /* only the width feeds into pin placement below */
    int per_side = def->pin_count / 2;

    /* Real DIP layout: pin 1 starts top-left (nearest the orientation notch,
       see render_ic_body) and counts down the left side; pin per_side+1
       continues at the bottom-right and counts back up to the top-right,
       ending opposite pin 1. Always exactly per_side pins on each side -
       side/position are derived from pin_number, not authored per IC. */
    for (int i = 0; i < c->pin_count; i++) {
        const IC_PinDef *pd = &def->pins[i];
        Pin *p = &c->pins[i];
        p->name = pd->name;
        p->direction = pd->direction;
        p->value = SIG_UNKNOWN;
        p->decorative = pd->decorative;
        /* stub tips sit one cell outside the body so wires attach past the
           edge, matching a schematic symbol rather than a flush PCB footprint */
        if (pd->pin_number <= per_side) {
            p->local_dx = -1;
            p->local_dy = pd->pin_number;
        } else {
            p->local_dx = body_w + 1;
            p->local_dy = per_side - (pd->pin_number - per_side) + 1;
        }
    }
}

/* Rotates a local offset (relative to the component's own origin, in its
   natural unrotated orientation) by `steps` quarter turns counterclockwise,
   re-deriving the natural body size (base_w/base_h) at each step so the
   result stays relative to the CURRENT (already-rotated-so-far) bounding
   box's top-left corner instead of drifting into negative coordinates - same
   requirement component_get_size's swapped w/h already assumes. One step is
   (x,y) -> (y, base_w - x): this maps every corner of the base_w x base_h
   rectangle to a corner of the base_h x base_w rectangle with its own
   top-left back at the origin (checked against all 4 corners by hand - see
   the design discussion this was built from). Applying it `steps` times
   (0-3, wrapped) composes to 90/180/270 degrees. */
static void rotate_local_offset(int x, int y, int base_w, int base_h, int steps, int *out_x, int *out_y) {
    int cw = base_w, ch = base_h;
    for (int i = 0; i < (steps & 3); i++) {
        int nx = y;
        int ny = cw - x;
        x = nx;
        y = ny;
        int t = cw; cw = ch; ch = t;
    }
    *out_x = x;
    *out_y = y;
}

void component_pin_world_pos(const Component *c, int pin_index, int *out_x, int *out_y) {
    int base_w, base_h;
    ic_dip_body_size(c->ic_def->pin_count, &base_w, &base_h);
    int rx, ry;
    rotate_local_offset(c->pins[pin_index].local_dx, c->pins[pin_index].local_dy, base_w, base_h, c->rotation, &rx, &ry);
    *out_x = c->grid_x + rx;
    *out_y = c->grid_y + ry;
}

int component_find_pin_at(const Component *c, int x, int y) {
    for (int i = 0; i < c->pin_count; i++) {
        int px, py;
        component_pin_world_pos(c, i, &px, &py);
        if (px == x && py == y) {
            return i;
        }
    }
    return -1;
}

void component_get_size(const Component *c, int *out_w, int *out_h) {
    ic_dip_body_size(c->ic_def->pin_count, out_w, out_h);
    if (c->rotation & 1) {
        int t = *out_w; *out_w = *out_h; *out_h = t;
    }
}
