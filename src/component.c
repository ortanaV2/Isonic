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

void component_pin_world_pos(const Component *c, int pin_index, int *out_x, int *out_y) {
    *out_x = c->grid_x + c->pins[pin_index].local_dx;
    *out_y = c->grid_y + c->pins[pin_index].local_dy;
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
}
