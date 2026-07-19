#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "circuit.h"

typedef struct {
    int x, y;
    int uf_idx;
    int layer_slot; /* wire's layer, or -1 for a pin (pins aren't layer-tagged themselves) */
} PointEntry;

void circuit_init(Circuit *circuit) {
    memset(circuit, 0, sizeof(Circuit));
    for (int i = 0; i < MAX_WIRES; i++) {
        circuit->wire_values[i] = SIG_UNKNOWN;
    }
    for (int i = 0; i < TOTAL_POINTS; i++) {
        circuit->pin_net[i] = i;
    }

    static const struct { const char *name; unsigned char r, g, b; LayerRole role; } defaults[4] = {
        { "TOP-Signal",    235, 220, 40,  LAYER_ROLE_NORMAL },
        { "GND",           60,  110, 235, LAYER_ROLE_GND },
        { "+5V",           235, 150, 40,  LAYER_ROLE_POWER },
        { "BOTTOM-Signal", 170, 90,  235, LAYER_ROLE_NORMAL },
    };
    for (int i = 0; i < 4; i++) {
        circuit->layers[i].in_use = 1;
        snprintf(circuit->layers[i].name, sizeof(circuit->layers[i].name), "%s", defaults[i].name);
        circuit->layers[i].color_r = defaults[i].r;
        circuit->layers[i].color_g = defaults[i].g;
        circuit->layers[i].color_b = defaults[i].b;
        circuit->layers[i].role = defaults[i].role;
        circuit->layer_order[i] = i;
    }
    circuit->layer_order_count = 4;
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

static int find_free_via_slot(Circuit *circuit) {
    for (int i = 0; i < MAX_VIAS; i++) {
        if (!circuit->vias[i].in_use) return i;
    }
    return -1;
}

static int find_free_layer_slot(Circuit *circuit) {
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (!circuit->layers[i].in_use) return i;
    }
    return -1;
}

/* True if a via at (x,y) bridges layer_a/layer_b, in either order. */
static int via_bridges(const Circuit *circuit, int x, int y, int layer_a, int layer_b) {
    for (int i = 0; i < circuit->via_high_water; i++) {
        const Via *v = &circuit->vias[i];
        if (!v->in_use || v->x != x || v->y != y) continue;
        if ((v->layer_slot_a == layer_a && v->layer_slot_b == layer_b) ||
            (v->layer_slot_a == layer_b && v->layer_slot_b == layer_a)) {
            return 1;
        }
    }
    return 0;
}

/* True if layer_slot is currently the top or bottom entry of layer_order -
   the two layers component pins bridge to (see circuit_rebuild_nets). */
static int layer_is_outer(const Circuit *circuit, int layer_slot) {
    if (circuit->layer_order_count <= 0) return 0;
    return layer_slot == circuit->layer_order[0] ||
           layer_slot == circuit->layer_order[circuit->layer_order_count - 1];
}

/* Places a via without rebuilding nets (callers that place several, or that
   are about to rebuild anyway right after, do it once themselves) - no-ops
   if that exact layer pair is already bridged there. */
static int add_via_raw(Circuit *circuit, int x, int y, int layer_slot_a, int layer_slot_b) {
    if (layer_slot_a == layer_slot_b) return -1;
    if (via_bridges(circuit, x, y, layer_slot_a, layer_slot_b)) return -1;
    int idx = find_free_via_slot(circuit);
    if (idx < 0) return -1;
    Via *v = &circuit->vias[idx];
    v->in_use = 1;
    v->x = x;
    v->y = y;
    v->layer_slot_a = layer_slot_a;
    v->layer_slot_b = layer_slot_b;
    if (idx + 1 > circuit->via_high_water) circuit->via_high_water = idx + 1;
    return idx;
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

/* True if any in-use wire touches (px,py) at all - an endpoint, or merely
   passing through mid-span - used to tell whether a via still has anything
   left to bridge there (see circuit_remove_wire). */
static int point_touches_any_wire(const Circuit *circuit, int px, int py) {
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        if ((w->from_x == px && w->from_y == py) || (w->to_x == px && w->to_y == py)) return 1;
        if (point_on_segment_interior(px, py, w->from_x, w->from_y, w->to_x, w->to_y)) return 1;
    }
    return 0;
}

/* If (px,py) coincides with, or lands mid-span on, any existing wire on a
   DIFFERENT layer than layer_slot, bridges the two layers there with an
   automatic via instead of silently merging the geometry the way a
   same-layer touch does (see Via's doc comment in circuit.h). */
static void auto_via_at_point(Circuit *circuit, int px, int py, int layer_slot) {
    int hw = circuit->wire_high_water;
    for (int i = 0; i < hw; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use || w->layer_slot == layer_slot) continue;
        int touches = (w->from_x == px && w->from_y == py) ||
                      (w->to_x == px && w->to_y == py) ||
                      point_on_segment_interior(px, py, w->from_x, w->from_y, w->to_x, w->to_y);
        if (touches) add_via_raw(circuit, px, py, layer_slot, w->layer_slot);
    }
}

/* Splits wire[wire_idx] into two independent wires at (px, py), which must lie
   strictly inside it. The original keeps its "from" end (and therefore its
   kind/terminal, see Wire's from_x/from_y comment) and simply gets shortened;
   the new tail piece is always a plain WIRE_KIND_NORMAL segment, on the same
   layer as the wire it was split from. */
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
    nw->layer_slot = w->layer_slot;
    if (new_idx + 1 > circuit->wire_high_water) circuit->wire_high_water = new_idx + 1;

    w->to_x = px;
    w->to_y = py;
}

/* Splits every existing wire ON THE SAME LAYER that (px, py) lands on the
   interior of, so a new wire ending/starting there forms a real junction
   between independent wires instead of just tapping into one long unbroken
   segment. A different-layer wire whose interior contains the point is
   bridged with an automatic via instead (see auto_via_at_point) - never
   silently spliced into this layer's geometry. */
static void split_wires_containing_point(Circuit *circuit, int px, int py, int layer_slot) {
    int hw = circuit->wire_high_water;
    for (int i = 0; i < hw; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use || w->layer_slot != layer_slot) continue;
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
   point where an existing SAME-LAYER wire's endpoint lands on this new wire's
   interior - the mirror image of split_wires_containing_point above. A
   different-layer existing endpoint landing on the new wire's path bridges
   an automatic via there instead (see auto_via_at_point), rather than
   splitting the new wire for it. Only the first segment carries kind/
   input_value, since the terminal end never moves; every segment shares
   layer_slot. Returns the id of the first segment, or -1 if none could be
   allocated. */
static int insert_wire_chain(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind, int layer_slot) {
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
            if (w->layer_slot != layer_slot) {
                auto_via_at_point(circuit, ex[e], ey[e], layer_slot);
                continue;
            }
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
        w->layer_slot = layer_slot;
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

int circuit_add_wire(Circuit *circuit, int from_x, int from_y, int to_x, int to_y, WireKind kind, int layer_slot) {
    if (from_x == to_x && from_y == to_y) return -1;

    /* If either endpoint of the new wire lands mid-span on an existing
       same-layer wire, split that wire there first so the result is a real
       junction between independent wires, not just a tap on one long
       unbroken segment. A different-layer wire there gets bridged with an
       automatic via instead (see auto_via_at_point). */
    split_wires_containing_point(circuit, from_x, from_y, layer_slot);
    split_wires_containing_point(circuit, to_x, to_y, layer_slot);
    auto_via_at_point(circuit, from_x, from_y, layer_slot);
    auto_via_at_point(circuit, to_x, to_y, layer_slot);

    /* Mirror image: pre-split the new wire itself at any existing same-layer
       endpoint that falls on its interior (insert_wire_chain also handles
       the different-layer auto-via case for its own interior touches). */
    int first_idx = insert_wire_chain(circuit, from_x, from_y, to_x, to_y, kind, layer_slot);

    circuit_rebuild_nets(circuit);
    return first_idx;
}

void circuit_remove_wire(Circuit *circuit, int wire_id) {
    if (wire_id < 0 || wire_id >= MAX_WIRES) return;
    if (!circuit->wires[wire_id].in_use) return;
    circuit->wires[wire_id].in_use = 0;
    /* a via with nothing left touching its point - no wire endpoint, and no
       wire merely passing through mid-span either - has nothing left to
       bridge, so it goes with the wire, same as a via getting desoldered
       once the last trace touching it is removed */
    for (int i = 0; i < circuit->via_high_water; i++) {
        Via *v = &circuit->vias[i];
        if (v->in_use && !point_touches_any_wire(circuit, v->x, v->y)) {
            v->in_use = 0;
        }
    }
    circuit_rebuild_nets(circuit);
}

int circuit_add_via(Circuit *circuit, int x, int y, int layer_slot_a, int layer_slot_b) {
    int idx = add_via_raw(circuit, x, y, layer_slot_a, layer_slot_b);
    if (idx >= 0) circuit_rebuild_nets(circuit);
    return idx;
}

void circuit_remove_via(Circuit *circuit, int via_id) {
    if (via_id < 0 || via_id >= MAX_VIAS) return;
    if (!circuit->vias[via_id].in_use) return;
    circuit->vias[via_id].in_use = 0;
    circuit_rebuild_nets(circuit);
}

int circuit_find_via_at(const Circuit *circuit, int x, int y) {
    for (int i = 0; i < circuit->via_high_water; i++) {
        const Via *v = &circuit->vias[i];
        if (v->in_use && v->x == x && v->y == y) return i;
    }
    return -1;
}

int circuit_layer_in_use(const Circuit *circuit, int layer_slot) {
    for (int i = 0; i < circuit->wire_high_water; i++) {
        if (circuit->wires[i].in_use && circuit->wires[i].layer_slot == layer_slot) return 1;
    }
    return 0;
}

int circuit_add_layer(Circuit *circuit, const char *name, unsigned char r, unsigned char g, unsigned char b) {
    if (circuit->layer_order_count >= MAX_LAYERS) return -1;
    int slot = find_free_layer_slot(circuit);
    if (slot < 0) return -1;
    Layer *l = &circuit->layers[slot];
    l->in_use = 1;
    snprintf(l->name, sizeof(l->name), "%s", name);
    l->color_r = r;
    l->color_g = g;
    l->color_b = b;
    l->role = LAYER_ROLE_NORMAL;
    circuit->layer_order[circuit->layer_order_count] = slot;
    circuit->layer_order_count++;
    return circuit->layer_order_count - 1;
}

int circuit_remove_layer(Circuit *circuit, int layer_order_pos) {
    if (layer_order_pos < 0 || layer_order_pos >= circuit->layer_order_count) return 0;
    int slot = circuit->layer_order[layer_order_pos];
    if (circuit->layers[slot].role != LAYER_ROLE_NORMAL) return 0;
    if (circuit_layer_in_use(circuit, slot)) return 0;
    /* also blocked if any via still references it - would otherwise leave a
       dangling bridge to a layer that no longer exists */
    for (int i = 0; i < circuit->via_high_water; i++) {
        if (circuit->vias[i].in_use &&
            (circuit->vias[i].layer_slot_a == slot || circuit->vias[i].layer_slot_b == slot)) {
            return 0;
        }
    }
    circuit->layers[slot].in_use = 0;
    for (int i = layer_order_pos; i < circuit->layer_order_count - 1; i++) {
        circuit->layer_order[i] = circuit->layer_order[i + 1];
    }
    circuit->layer_order_count--;
    circuit_rebuild_nets(circuit); /* outer-layer pin bridging may have changed */
    return 1;
}

void circuit_move_layer(Circuit *circuit, int layer_order_pos, int direction) {
    if (layer_order_pos < 0 || layer_order_pos >= circuit->layer_order_count) return;
    int other = layer_order_pos + direction;
    if (other < 0 || other >= circuit->layer_order_count) return;
    int tmp = circuit->layer_order[layer_order_pos];
    circuit->layer_order[layer_order_pos] = circuit->layer_order[other];
    circuit->layer_order[other] = tmp;
    circuit_rebuild_nets(circuit); /* outer-layer pin bridging may have changed */
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

    /* a wire's own two ends are always one net, regardless of layer rules
       below - it's a single physical trace */
    for (int i = 0; i < circuit->wire_high_water; i++) {
        if (!circuit->wires[i].in_use) continue;
        uf_union(circuit, WIRE_POINT_ID(i, 0), WIRE_POINT_ID(i, 1));
    }

    /* GND/+5V behave as one continuous plane each, not routed traces - union
       every wire on such a layer together regardless of position (see
       LayerRole's doc comment in circuit.h). */
    for (int role_i = 0; role_i < 2; role_i++) {
        LayerRole role = (role_i == 0) ? LAYER_ROLE_GND : LAYER_ROLE_POWER;
        int rep = -1;
        for (int i = 0; i < circuit->wire_high_water; i++) {
            Wire *w = &circuit->wires[i];
            if (!w->in_use || circuit->layers[w->layer_slot].role != role) continue;
            if (rep < 0) rep = WIRE_POINT_ID(i, 0);
            else uf_union(circuit, WIRE_POINT_ID(i, 0), rep);
        }
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
            poi[poi_count].layer_slot = -1;
            poi_count++;
        }
    }
    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        poi[poi_count].x = w->from_x;
        poi[poi_count].y = w->from_y;
        poi[poi_count].uf_idx = WIRE_POINT_ID(i, 0);
        poi[poi_count].layer_slot = w->layer_slot;
        poi_count++;
        poi[poi_count].x = w->to_x;
        poi[poi_count].y = w->to_y;
        poi[poi_count].uf_idx = WIRE_POINT_ID(i, 1);
        poi[poi_count].layer_slot = w->layer_slot;
        poi_count++;
    }

    qsort(poi, poi_count, sizeof(PointEntry), compare_point_entry);

    /* Points sharing a coordinate only union pairwise when the connection is
       actually legal: two pins always connect; a pin connects to a wire only
       if that wire is on one of the two outer layers (real through-hole pins
       only reach the top/bottom copper, see layer_is_outer); two wires
       connect if they're on the same layer, or a via at this exact point
       bridges their two layers. Groups are always small in practice (a
       handful of things ever share one grid point), so the O(n^2) pairwise
       check here is cheap. */
    int k = 0;
    while (k < poi_count) {
        int j = k + 1;
        while (j < poi_count && poi[j].x == poi[k].x && poi[j].y == poi[k].y) j++;
        for (int a = k; a < j; a++) {
            for (int b = a + 1; b < j; b++) {
                int a_is_pin = poi[a].layer_slot < 0;
                int b_is_pin = poi[b].layer_slot < 0;
                int should_union;
                if (a_is_pin && b_is_pin) {
                    should_union = 1;
                } else if (a_is_pin || b_is_pin) {
                    int wire_layer = a_is_pin ? poi[b].layer_slot : poi[a].layer_slot;
                    should_union = layer_is_outer(circuit, wire_layer);
                } else {
                    should_union = (poi[a].layer_slot == poi[b].layer_slot) ||
                                   via_bridges(circuit, poi[k].x, poi[k].y, poi[a].layer_slot, poi[b].layer_slot);
                }
                if (should_union) uf_union(circuit, poi[a].uf_idx, poi[b].uf_idx);
            }
        }
        if (j - k >= 3) add_junction(circuit, poi[k].x, poi[k].y);
        k = j;
    }

    /* Pin-tip-lands-on-a-wire's-interior is still detected generically here
       (e.g. placing/dragging a component so a pin taps into an existing
       wire), but a WIRE endpoint landing on another wire's interior is
       deliberately NOT - that's restricted to poi[m].uf_idx < MAX_GLOBAL_PINS
       (pins only) below. Wire-to-wire mid-span taps are only ever created by
       an explicit new-wire draw (circuit_add_wire pre-splits/auto-vias both
       wires itself, before either even reaches this function - see
       split_wires_containing_point/insert_wire_chain/auto_via_at_point
       above), never as a side effect of merely dragging an existing wire
       across another one. Same outer-layer rule as the coincidence pass
       above: a pin only taps into a wire on one of the two outer layers. */
    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use || !layer_is_outer(circuit, w->layer_slot)) continue;
        for (int m = 0; m < poi_count; m++) {
            if (poi[m].uf_idx >= MAX_GLOBAL_PINS) continue;
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

int circuit_find_wire_node_near(const Circuit *circuit, float fx, float fy, float tolerance, int *out_x, int *out_y) {
    float best_d2 = tolerance * tolerance;
    int found = 0;
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        float dfx = fx - (float)w->from_x, dfy = fy - (float)w->from_y;
        float d0 = dfx * dfx + dfy * dfy;
        if (d0 <= best_d2) { best_d2 = d0; *out_x = w->from_x; *out_y = w->from_y; found = 1; }
        float dtx = fx - (float)w->to_x, dty = fy - (float)w->to_y;
        float d1 = dtx * dtx + dty * dty;
        if (d1 <= best_d2) { best_d2 = d1; *out_x = w->to_x; *out_y = w->to_y; found = 1; }
    }
    return found;
}

int circuit_wire_layer_at_point(const Circuit *circuit, int x, int y) {
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        if ((w->from_x == x && w->from_y == y) || (w->to_x == x && w->to_y == y)) return w->layer_slot;
    }
    return -1;
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
