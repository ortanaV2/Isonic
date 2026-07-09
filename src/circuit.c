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
    for (int i = 0; i < MAX_WIRES; i++) {
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

/* Strict interior test: true if P is collinear with and strictly between A and B
   (excludes the endpoints themselves - those are handled by the coincidence pass
   in circuit_rebuild_nets). */
static int point_on_segment_interior(int px, int py, int ax, int ay, int bx, int by) {
    long long cross = (long long)(px - ax) * (by - ay) - (long long)(py - ay) * (bx - ax);
    if (cross != 0) return 0;
    long long dot = (long long)(px - ax) * (bx - ax) + (long long)(py - ay) * (by - ay);
    if (dot <= 0) return 0;
    long long len2 = (long long)(bx - ax) * (bx - ax) + (long long)(by - ay) * (by - ay);
    return dot < len2;
}

/* Splits wire[wire_idx] into two independent wires at (px, py), which must lie
   strictly inside it. The original keeps its "from" end (and therefore its
   kind/terminal, see Wire's from_x/from_y comment) and simply gets shortened;
   the new tail piece is always a plain WIRE_KIND_NORMAL segment. */
static void split_wire_slot(Circuit *circuit, int wire_idx, int px, int py) {
    int new_idx = find_free_wire_slot(circuit);
    if (new_idx < 0) return;
    Wire *w = &circuit->wires[wire_idx];
    Wire *nw = &circuit->wires[new_idx];
    nw->in_use = 1;
    nw->from_x = px;
    nw->from_y = py;
    nw->to_x = w->to_x;
    nw->to_y = w->to_y;
    nw->selected = 0;
    nw->kind = WIRE_KIND_NORMAL;
    nw->input_value = 0;
    if (new_idx + 1 > circuit->wire_high_water) circuit->wire_high_water = new_idx + 1;

    w->to_x = px;
    w->to_y = py;
}

/* Splits every existing wire that (px, py) lands on the interior of, so a new
   wire ending/starting there forms a real junction between independent wires
   instead of just tapping into one long unbroken segment. */
static void split_wires_containing_point(Circuit *circuit, int px, int py) {
    int hw = circuit->wire_high_water;
    for (int i = 0; i < hw; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        if (point_on_segment_interior(px, py, w->from_x, w->from_y, w->to_x, w->to_y)) {
            split_wire_slot(circuit, i, px, py);
        }
    }
}

typedef struct {
    int x, y;
    long long t; /* position along the new segment, for ordering the split points */
} SplitPoint;

static int compare_split_point(const void *pa, const void *pb) {
    long long ta = ((const SplitPoint *)pa)->t;
    long long tb = ((const SplitPoint *)pb)->t;
    return (ta > tb) - (ta < tb);
}

/* Adds (from -> to) as one or more consecutive wire segments, pre-split at any
   point where an existing wire's endpoint lands on this new wire's interior -
   the mirror image of split_wires_containing_point above. Only the first
   segment carries kind/input_value, since the terminal end never moves.
   Returns the id of the first segment, or -1 if none could be allocated. */
static int insert_wire_chain(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind) {
    static SplitPoint pts[MAX_WIRES * 2];
    int pt_count = 0;
    long long dx = to_x - from_x, dy = to_y - from_y;

    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        int ex[2] = { w->from_x, w->to_x };
        int ey[2] = { w->from_y, w->to_y };
        for (int e = 0; e < 2; e++) {
            if (!point_on_segment_interior(ex[e], ey[e], from_x, from_y, to_x, to_y)) continue;
            int dup = 0;
            for (int k = 0; k < pt_count; k++) {
                if (pts[k].x == ex[e] && pts[k].y == ey[e]) { dup = 1; break; }
            }
            if (dup || pt_count >= MAX_WIRES * 2) continue;
            pts[pt_count].x = ex[e];
            pts[pt_count].y = ey[e];
            pts[pt_count].t = (long long)(ex[e] - from_x) * dx + (long long)(ey[e] - from_y) * dy;
            pt_count++;
        }
    }
    qsort(pts, pt_count, sizeof(SplitPoint), compare_split_point);

    int first_idx = -1;
    int seg_from_x = from_x, seg_from_y = from_y;
    WireKind seg_kind = kind;
    for (int i = 0; i <= pt_count; i++) {
        int seg_to_x = (i < pt_count) ? pts[i].x : to_x;
        int seg_to_y = (i < pt_count) ? pts[i].y : to_y;
        int idx = find_free_wire_slot(circuit);
        if (idx < 0) break;
        Wire *w = &circuit->wires[idx];
        w->in_use = 1;
        w->from_x = seg_from_x;
        w->from_y = seg_from_y;
        w->to_x = seg_to_x;
        w->to_y = seg_to_y;
        w->selected = 0;
        w->kind = seg_kind;
        w->input_value = 0;
        if (idx + 1 > circuit->wire_high_water) circuit->wire_high_water = idx + 1;
        if (first_idx < 0) first_idx = idx;
        seg_from_x = seg_to_x;
        seg_from_y = seg_to_y;
        seg_kind = WIRE_KIND_NORMAL;
    }
    return first_idx;
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

    /* If either endpoint of the new wire lands mid-span on an existing wire,
       split that wire there first so the result is a real junction between
       independent wires, not just a tap on one long unbroken segment. */
    split_wires_containing_point(circuit, from_x, from_y);
    split_wires_containing_point(circuit, to_x, to_y);

    /* Mirror image: pre-split the new wire itself at any existing endpoint
       that falls on its interior. */
    int first_idx = insert_wire_chain(circuit, from_x, from_y, to_x, to_y, kind);

    circuit_rebuild_nets(circuit);
    return first_idx;
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
