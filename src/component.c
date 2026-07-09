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

    int left_seen = 0, right_seen = 0;
    for (int i = 0; i < c->pin_count; i++) {
        const IC_PinDef *pd = &def->pins[i];
        Pin *p = &c->pins[i];
        p->name = pd->name;
        p->direction = pd->direction;
        p->value = SIG_UNKNOWN;
        /* stub tips sit one cell outside the body so wires attach past the
           edge, matching a schematic symbol rather than a flush PCB footprint */
        if (pd->side == 0) {
            p->local_dx = -1;
            p->local_dy = 1 + left_seen;
            left_seen++;
        } else {
            p->local_dx = def->width + 1;
            p->local_dy = 1 + right_seen;
            right_seen++;
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
    *out_w = c->ic_def->width;
    *out_h = c->ic_def->height;
}
