#include <stdio.h>
#include <string.h>
#include "diagnostics.h"

/* Matches the real 74HC-series fan-out spec quoted earlier in this project's
   history (10 LSTTL-equivalent loads) - not configurable per-IC since every
   IC in the catalog is from a comparable-drive-strength family. */
#define DIAG_FAN_OUT_THRESHOLD 10

/* Per-net scratch bookkeeping, rebuilt fresh every call - circuits here are
   small (at most a few hundred wires/pins), so a simple linear-scan net
   table redone every frame is plenty fast; no need to cache it across frames. */
#define NET_MAX 1024
#define NET_MAX_WIRES 64
#define NET_MAX_DRIVERS 32
#define NET_MAX_LOADS 256

typedef struct {
    int root;
    int wire_ids[NET_MAX_WIRES];
    int wire_count;
    /* every pin sharing this net, regardless of direction or drive state
       (power pins and tri-stated outputs included) - purely topological, so
       the floating-input check can ask "is this pin physically alone on its
       net" without depending on live signal values, unlike out_pin_count
       below which deliberately excludes tri-stated (SIG_HIZ) outputs. */
    int pin_total;
    /* driving PIN_OUTPUT pins (value != SIG_HIZ - a tri-stated output isn't
       really driving anything right now, same rule sim.c's gather_drivers uses) */
    struct { int component_id, pin_index; } out_pins[NET_MAX_DRIVERS];
    int out_pin_count;
    int in_wire_count; /* WIRE_KIND_INPUT wires driving this net - always active drivers */
    /* PIN_INPUT pins on this net, for fan-out counting */
    struct { int component_id, pin_index; } in_pins[NET_MAX_LOADS];
    int in_pin_count;
    /* set if sim_step already resolved this net to SIG_CONFLICT - i.e. its
       active drivers actually disagree on value, not just multiple of them
       agreeing on the same level (which is electrically harmless: two
       push-pull outputs both driving HIGH, or both driving LOW, don't fight
       each other, only opposing levels do) */
    int has_conflict;
} NetInfo;

static NetInfo g_nets[NET_MAX];
static int g_net_count;

static NetInfo *find_or_add_net(int root) {
    for (int i = 0; i < g_net_count; i++) {
        if (g_nets[i].root == root) return &g_nets[i];
    }
    if (g_net_count >= NET_MAX) return NULL;
    NetInfo *n = &g_nets[g_net_count++];
    memset(n, 0, sizeof(*n));
    n->root = root;
    return n;
}

/* Read-only lookup - NULL if that root has no recorded net (e.g. it
   overflowed NET_MAX during collect_nets). */
static NetInfo *find_net(int root) {
    for (int i = 0; i < g_net_count; i++) {
        if (g_nets[i].root == root) return &g_nets[i];
    }
    return NULL;
}

static void collect_nets(Circuit *circuit) {
    g_net_count = 0;

    for (int wi = 0; wi < circuit->wire_high_water; wi++) {
        Wire *w = &circuit->wires[wi];
        if (!w->in_use) continue;
        NetInfo *n = find_or_add_net(circuit_wire_net_root(circuit, wi));
        if (n == NULL) continue;
        if (n->wire_count < NET_MAX_WIRES) n->wire_ids[n->wire_count++] = wi;
        if (w->kind == WIRE_KIND_INPUT) n->in_wire_count++;
        if (circuit->wire_values[wi] == SIG_CONFLICT) n->has_conflict = 1;
    }

    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            Pin *p = &c->pins[pi];
            NetInfo *n = find_or_add_net(circuit_pin_net_root(circuit, ci, pi));
            if (n == NULL) continue;
            /* pin_total counts EVERY pin on the net (power/HiZ included), for
               the floating check - done before the PIN_POWER skip below,
               which only governs driver/load bookkeeping, not net membership */
            n->pin_total++;
            if (p->direction == PIN_POWER) continue;
            if (p->value == SIG_CONFLICT) n->has_conflict = 1;
            if (p->direction == PIN_OUTPUT) {
                if (p->value != SIG_HIZ && n->out_pin_count < NET_MAX_DRIVERS) {
                    n->out_pins[n->out_pin_count].component_id = ci;
                    n->out_pins[n->out_pin_count].pin_index = pi;
                    n->out_pin_count++;
                }
            } else { /* PIN_INPUT */
                if (n->in_pin_count < NET_MAX_LOADS) {
                    n->in_pins[n->in_pin_count].component_id = ci;
                    n->in_pins[n->in_pin_count].pin_index = pi;
                    n->in_pin_count++;
                }
            }
        }
    }
}

static void diag_add_wires(Diagnostic *d, const int *wire_ids, int count) {
    for (int i = 0; i < count && d->wire_count < DIAG_MAX_WIRES; i++) {
        d->wire_ids[d->wire_count++] = wire_ids[i];
    }
}

static void diag_add_pin(Diagnostic *d, int component_id, int pin_index) {
    if (d->pin_count < DIAG_MAX_PINS) {
        d->pins[d->pin_count].component_id = component_id;
        d->pins[d->pin_count].pin_index = pin_index;
        d->pin_count++;
    }
}

static void check_bus_conflict_and_overwrite(DiagnosticSet *out) {
    for (int ni = 0; ni < g_net_count && out->count < DIAG_MAX; ni++) {
        NetInfo *n = &g_nets[ni];
        int total_drivers = n->out_pin_count + n->in_wire_count;
        /* only a genuine value disagreement is electrically dangerous - see
           has_conflict's doc comment on NetInfo */
        if (total_drivers < 2 || !n->has_conflict) continue;

        Diagnostic *d = &out->items[out->count];
        memset(d, 0, sizeof(*d));
        diag_add_wires(d, n->wire_ids, n->wire_count);
        for (int i = 0; i < n->out_pin_count; i++) {
            diag_add_pin(d, n->out_pins[i].component_id, n->out_pins[i].pin_index);
        }

        if (n->in_wire_count >= 1 && n->out_pin_count >= 1) {
            d->severity = DIAG_ERROR;
            d->category = DIAG_OVERWRITE;
            snprintf(d->summary, sizeof(d->summary), "Overwrite");
            snprintf(d->detail, sizeof(d->detail),
                     "An Input wire shares a net with %d IC output pin%s, forcing its value onto a line the chip "
                     "is also driving. This can short-circuit the output and damage the device.",
                     n->out_pin_count, n->out_pin_count == 1 ? "" : "s");
        } else {
            d->severity = DIAG_ERROR;
            d->category = DIAG_BUS_CONFLICT;
            snprintf(d->summary, sizeof(d->summary), "Bus Conflict");
            snprintf(d->detail, sizeof(d->detail),
                     "%d drivers are actively sharing this net. Multiple active outputs driving the same line "
                     "can cause current contention and damage the devices.",
                     total_drivers);
        }
        out->count++;
    }
}

static void check_fan_out(DiagnosticSet *out) {
    for (int ni = 0; ni < g_net_count && out->count < DIAG_MAX; ni++) {
        NetInfo *n = &g_nets[ni];
        if (n->out_pin_count + n->in_wire_count < 1) continue; /* an undriven net has nothing to overload */
        if (n->in_pin_count <= DIAG_FAN_OUT_THRESHOLD) continue;

        Diagnostic *d = &out->items[out->count];
        memset(d, 0, sizeof(*d));
        d->severity = DIAG_WARNING;
        d->category = DIAG_FAN_OUT;
        diag_add_wires(d, n->wire_ids, n->wire_count);
        for (int i = 0; i < n->in_pin_count; i++) {
            diag_add_pin(d, n->in_pins[i].component_id, n->in_pins[i].pin_index);
        }
        snprintf(d->summary, sizeof(d->summary), "Fan-Out");
        snprintf(d->detail, sizeof(d->detail),
                 "This net drives %d inputs, exceeding the typical 74HC fan-out limit of %d loads. "
                 "The signal will not switch cleanly under this load.",
                 n->in_pin_count, DIAG_FAN_OUT_THRESHOLD);
        out->count++;
    }
}

/* All floating pins on one component are reported as a single stacked
   diagnostic (with a count) rather than one chip per pin - an IC with
   several unwired inputs would otherwise flood the bottom warning list
   with near-duplicate entries. */
static void check_floating_pins(Circuit *circuit, DiagnosticSet *out) {
    for (int ci = 0; ci < circuit->component_high_water && out->count < DIAG_MAX; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;

        Diagnostic *d = NULL; /* lazily claimed on this component's first floating pin */
        for (int pi = 0; pi < c->pin_count; pi++) {
            Pin *p = &c->pins[pi];
            if (p->direction != PIN_INPUT || p->decorative) continue;

            /* floating == physically alone on its own net: nothing else
               shares it (pin_total counts only this pin) and no wire is on
               it. Using net membership, not exact-point geometry, is what
               makes this respect the same connectivity rules
               circuit_rebuild_nets used: a pin tapped by a wire passing
               through mid-span (a bus line grazing a row of pins - see the
               mid-span pass in circuit_rebuild_nets), or connected across a
               chain of split wire segments, shares that wire's net and is
               correctly NOT floating, even though no wire ENDPOINT sits
               exactly on it (which the old circuit_point_connection_count
               test missed, flagging such pins as floating by mistake). It
               also correctly DOES flag a pin sitting only on an inner-layer
               wire, which never reaches a through-hole pin. */
            NetInfo *n = find_net(circuit_pin_net_root(circuit, ci, pi));
            if (n == NULL || n->wire_count > 0 || n->pin_total > 1) continue;

            if (d == NULL) {
                d = &out->items[out->count++];
                memset(d, 0, sizeof(*d));
                d->severity = DIAG_WARNING;
                d->category = DIAG_FLOATING_PIN;
            }
            diag_add_pin(d, ci, pi);
        }
        if (d == NULL) continue;

        const char *label = (c->ic_def != NULL) ? c->ic_def->name : "This component";
        if (d->pin_count > 1) {
            snprintf(d->summary, sizeof(d->summary), "Floating Pin x%d", d->pin_count);
            snprintf(d->detail, sizeof(d->detail),
                     "%s has %d unconnected input pins. A floating CMOS input can pick up noise or draw "
                     "excess current; tie each to a defined level.",
                     label, d->pin_count);
        } else {
            snprintf(d->summary, sizeof(d->summary), "Floating Pin");
            snprintf(d->detail, sizeof(d->detail),
                     "%s pin \"%s\" is not connected. A floating CMOS input can pick up noise or draw "
                     "excess current; tie it to a defined level.",
                     label, c->pins[d->pins[0].pin_index].name);
        }
    }
}

void diagnostics_compute(Circuit *circuit, DiagnosticSet *out) {
    out->count = 0;
    collect_nets(circuit);

    check_bus_conflict_and_overwrite(out);
    check_fan_out(out);
    check_floating_pins(circuit, out);
}

int diagnostic_has_wire(const Diagnostic *diag, int wire_id) {
    for (int i = 0; i < diag->wire_count; i++) {
        if (diag->wire_ids[i] == wire_id) return 1;
    }
    return 0;
}

int diagnostic_has_pin(const Diagnostic *diag, int component_id, int pin_index) {
    for (int i = 0; i < diag->pin_count; i++) {
        if (diag->pins[i].component_id == component_id && diag->pins[i].pin_index == pin_index) return 1;
    }
    return 0;
}
