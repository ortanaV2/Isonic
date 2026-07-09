#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "circuit.h"

typedef struct {
    int x, y;
    int uf_idx;
} PointEntry;

void circuit_init(Circuit *circuit) {
    memset(circuit, 0, sizeof(Circuit));
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        circuit->components[i].id = i;
    }
    for (int i = 0; i < MAX_WIRES; i++) {
        circuit->wires[i].id = i;
        circuit->wire_values[i] = SIG_UNKNOWN;
    }
    for (int i = 0; i < TOTAL_POINTS; i++) {
        circuit->pin_net[i] = i;
    }
}

static int find_free_component_slot(Circuit *circuit) {
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (!circuit->components[i].in_use) return i;
    }
    return -1;
}

static int find_free_wire_slot(Circuit *circuit) {
    for (int i = 0; i < MAX_WIRES; i++) {
        if (!circuit->wires[i].in_use) return i;
    }
    return -1;
}

int circuit_add_ic(Circuit *circuit, int grid_x, int grid_y, const IC_Def *def) {
    int idx = find_free_component_slot(circuit);
    if (idx < 0) return -1;
    Component *c = &circuit->components[idx];
    c->in_use = 1;
    component_init_ic(c, grid_x, grid_y, def);
    if (idx + 1 > circuit->component_high_water) circuit->component_high_water = idx + 1;
    return idx;
}

void circuit_remove_component(Circuit *circuit, int component_id) {
    if (component_id < 0 || component_id >= MAX_COMPONENTS) return;
    if (!circuit->components[component_id].in_use) return;
    /* wires are independent objects now (no back-reference to components);
       removing a component simply lets its pins drop out of the next net rebuild */
    circuit->components[component_id].in_use = 0;
    circuit_rebuild_nets(circuit);
}

int circuit_add_wire(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind) {
    if (from_x == to_x && from_y == to_y) return -1;
    int idx = find_free_wire_slot(circuit);
    if (idx < 0) return -1;

    Wire *w = &circuit->wires[idx];
    w->in_use = 1;
    w->from_x = from_x;
    w->from_y = from_y;
    w->to_x = to_x;
    w->to_y = to_y;
    w->selected = 0;
    w->kind = kind;
    w->input_value = 0;
    if (idx + 1 > circuit->wire_high_water) circuit->wire_high_water = idx + 1;

    circuit_rebuild_nets(circuit);
    return idx;
}

void circuit_remove_wire(Circuit *circuit, int wire_id) {
    if (wire_id < 0 || wire_id >= MAX_WIRES) return;
    if (!circuit->wires[wire_id].in_use) return;
    circuit->wires[wire_id].in_use = 0;
    circuit_rebuild_nets(circuit);
}

static int uf_find(Circuit *circuit, int x) {
    while (circuit->pin_net[x] != x) {
        circuit->pin_net[x] = circuit->pin_net[circuit->pin_net[x]]; /* path halving */
        x = circuit->pin_net[x];
    }
    return x;
}

static void uf_union(Circuit *circuit, int a, int b) {
    int ra = uf_find(circuit, a);
    int rb = uf_find(circuit, b);
    if (ra != rb) circuit->pin_net[ra] = rb;
}

static int compare_point_entry(const void *pa, const void *pb) {
    const PointEntry *a = (const PointEntry *)pa;
    const PointEntry *b = (const PointEntry *)pb;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

/* Strict interior test: true if P is collinear with and strictly between A and B
   (excludes the endpoints themselves - those are handled by the coincidence pass). */
static int point_on_segment_interior(int px, int py, int ax, int ay, int bx, int by) {
    long long cross = (long long)(px - ax) * (by - ay) - (long long)(py - ay) * (bx - ax);
    if (cross != 0) return 0;
    long long dot = (long long)(px - ax) * (bx - ax) + (long long)(py - ay) * (by - ay);
    if (dot <= 0) return 0;
    long long len2 = (long long)(bx - ax) * (bx - ax) + (long long)(by - ay) * (by - ay);
    return dot < len2;
}

static void add_junction(Circuit *circuit, int x, int y) {
    for (int i = 0; i < circuit->junction_count; i++) {
        if (circuit->junctions[i].x == x && circuit->junctions[i].y == y) return;
    }
    if (circuit->junction_count < MAX_JUNCTIONS) {
        circuit->junctions[circuit->junction_count].x = x;
        circuit->junctions[circuit->junction_count].y = y;
        circuit->junction_count++;
    }
}

void circuit_rebuild_nets(Circuit *circuit) {
    for (int i = 0; i < TOTAL_POINTS; i++) circuit->pin_net[i] = i;
    circuit->junction_count = 0;

    for (int i = 0; i < circuit->wire_high_water; i++) {
        if (!circuit->wires[i].in_use) continue;
        uf_union(circuit, WIRE_POINT_ID(i, 0), WIRE_POINT_ID(i, 1));
    }

    static PointEntry poi[TOTAL_POINTS];
    int poi_count = 0;

    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            int x, y;
            component_pin_world_pos(c, pi, &x, &y);
            poi[poi_count].x = x;
            poi[poi_count].y = y;
            poi[poi_count].uf_idx = GLOBAL_PIN_ID(ci, pi);
            poi_count++;
        }
    }
    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        poi[poi_count].x = w->from_x;
        poi[poi_count].y = w->from_y;
        poi[poi_count].uf_idx = WIRE_POINT_ID(i, 0);
        poi_count++;
        poi[poi_count].x = w->to_x;
        poi[poi_count].y = w->to_y;
        poi[poi_count].uf_idx = WIRE_POINT_ID(i, 1);
        poi_count++;
    }

    qsort(poi, poi_count, sizeof(PointEntry), compare_point_entry);

    int k = 0;
    while (k < poi_count) {
        int j = k + 1;
        while (j < poi_count && poi[j].x == poi[k].x && poi[j].y == poi[k].y) j++;
        for (int m = k + 1; m < j; m++) uf_union(circuit, poi[m].uf_idx, poi[k].uf_idx);
        if (j - k >= 3) add_junction(circuit, poi[k].x, poi[k].y);
        k = j;
    }

    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        for (int m = 0; m < poi_count; m++) {
            if (poi[m].uf_idx == WIRE_POINT_ID(i, 0) || poi[m].uf_idx == WIRE_POINT_ID(i, 1)) continue;
            if (point_on_segment_interior(poi[m].x, poi[m].y, w->from_x, w->from_y, w->to_x, w->to_y)) {
                uf_union(circuit, poi[m].uf_idx, WIRE_POINT_ID(i, 0));
                add_junction(circuit, poi[m].x, poi[m].y);
            }
        }
    }
}

int circuit_pin_net_root(Circuit *circuit, int component_id, int pin_index) {
    return uf_find(circuit, GLOBAL_PIN_ID(component_id, pin_index));
}

int circuit_wire_net_root(Circuit *circuit, int wire_id) {
    return uf_find(circuit, WIRE_POINT_ID(wire_id, 0));
}

int circuit_find_component_at(const Circuit *circuit, int x, int y) {
    for (int i = circuit->component_high_water - 1; i >= 0; i--) {
        const Component *c = &circuit->components[i];
        if (!c->in_use) continue;
        int w, h;
        component_get_size(c, &w, &h);
        if (x >= c->grid_x && x < c->grid_x + w && y >= c->grid_y && y < c->grid_y + h) {
            return i;
        }
    }
    return -1;
}

int circuit_find_pin_at(const Circuit *circuit, int x, int y, int *out_component_id, int *out_pin_index) {
    for (int i = circuit->component_high_water - 1; i >= 0; i--) {
        const Component *c = &circuit->components[i];
        if (!c->in_use) continue;
        int pin_index = component_find_pin_at(c, x, y);
        if (pin_index >= 0) {
            *out_component_id = i;
            *out_pin_index = pin_index;
            return 1;
        }
    }
    return 0;
}

static float dist_point_to_segment(float px, float py, float ax, float ay, float bx, float by) {
    float abx = bx - ax, aby = by - ay;
    float apx = px - ax, apy = py - ay;
    float len2 = abx * abx + aby * aby;
    float t = (len2 > 0.0001f) ? (apx * abx + apy * aby) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = ax + t * abx, cy = ay + t * aby;
    float dx = px - cx, dy = py - cy;
    return sqrtf(dx * dx + dy * dy);
}

int circuit_find_wire_at(const Circuit *circuit, float fx, float fy, float tolerance) {
    int best_id = -1;
    float best_dist = tolerance;
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        float d = dist_point_to_segment(fx, fy, (float)w->from_x, (float)w->from_y, (float)w->to_x, (float)w->to_y);
        if (d <= best_dist) {
            best_dist = d;
            best_id = i;
        }
    }
    return best_id;
}

int circuit_point_connection_count(const Circuit *circuit, int x, int y) {
    int count = 0;
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        if (w->from_x == x && w->from_y == y) count++;
        if (w->to_x == x && w->to_y == y) count++;
    }
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        const Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            int px, py;
            component_pin_world_pos(c, pi, &px, &py);
            if (px == x && py == y) count++;
        }
    }
    return count;
}

int circuit_point_is_junction(const Circuit *circuit, int x, int y) {
    for (int i = 0; i < circuit->junction_count; i++) {
        if (circuit->junctions[i].x == x && circuit->junctions[i].y == y) return 1;
    }
    return 0;
}

int circuit_footprint_overlaps(const Circuit *circuit, int x, int y, int w, int h, int ignore_id) {
    for (int i = 0; i < circuit->component_high_water; i++) {
        if (i == ignore_id) continue;
        const Component *c = &circuit->components[i];
        if (!c->in_use) continue;
        int cw, ch;
        component_get_size(c, &cw, &ch);
        int overlap_x = x < c->grid_x + cw && x + w > c->grid_x;
        int overlap_y = y < c->grid_y + ch && y + h > c->grid_y;
        if (overlap_x && overlap_y) return 1;
    }
    return 0;
}
