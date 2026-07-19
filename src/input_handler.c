#include "app.h"
#include "render.h"
#include "undo.h"

/* Clears every .selected flag in the circuit, not just the tracked single
   ids - a marquee selection (see finish_marquee_select) can mark several
   components/wires at once without ever touching selected_component_id/
   selected_wire_id, so those alone aren't enough to undo it. */
static void clear_selection(App *app) {
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        app->circuit.components[i].selected = 0;
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        app->circuit.wires[i].selected = 0;
    }
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
}

static void select_component(App *app, int id) {
    clear_selection(app);
    app->selected_component_id = id;
    app->circuit.components[id].selected = 1;
}

static void select_wire(App *app, int id) {
    clear_selection(app);
    app->selected_wire_id = id;
    app->circuit.wires[id].selected = 1;
}

/* Removes every selected component/wire, not just the single tracked ids -
   a marquee selection can mark several at once (see clear_selection above). */
static void delete_selection(App *app) {
    int removed = 0;
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        if (app->circuit.components[i].in_use && app->circuit.components[i].selected) {
            circuit_remove_component(&app->circuit, i);
            removed = 1;
        }
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        if (app->circuit.wires[i].in_use && app->circuit.wires[i].selected) {
            circuit_remove_wire(&app->circuit, i);
            removed = 1;
        }
    }
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
    if (removed) undo_push(&app->circuit);
}

/* Unmarks the wires temporarily highlighted for a DRAG_WIRE_NODE (see
   drag_node_* in app.h) - they aren't tracked by selected_wire_id, since
   several wires can share one node, so they need their own cleanup. */
static void clear_wire_node_marks(App *app) {
    for (int i = 0; i < app->drag_node_count; i++) {
        app->circuit.wires[app->drag_node_wire_id[i]].selected = 0;
    }
    app->drag_node_count = 0;
    app->drag_node_via_count = 0;
}

static void cancel_transient_actions(App *app) {
    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
    app->panning = 0;
    app->marquee_active = 0;
    app->pasting = 0;
    app->taskbar.menu_open = 0;
    app->active_tool = TOOL_SELECT;
}

/* Escape backs out of exactly one thing per press, in priority order - not
   everything transient at once (that's what cancel_transient_actions above
   is for, used when switching tools/pasting, a deliberate "start fresh"
   moment rather than a "step back" one). Pressing Escape while placing a
   part with the Manage Data panel open should only cancel the placement,
   not also close the panel underneath it, which has nothing to do with
   what the user was actually backing out of - so each level only falls
   through to the next once everything above it is already inactive. */
static void handle_escape(App *app) {
    if (app_pending_place_ic(app) != NULL) {
        app->pasting = 0;
        if (app->active_tool == TOOL_PLACE_IC) app->active_tool = TOOL_SELECT;
        app->place_ic_name = NULL;
        return;
    }
    if (app->wiring) {
        app->wiring = 0;
        app->wiring_kind = WIRE_KIND_NORMAL;
        return;
    }
    if (app->drag_kind != DRAG_NONE) {
        clear_wire_node_marks(app);
        app->drag_kind = DRAG_NONE;
        app->drag_attach_count = 0;
        return;
    }
    if (app->marquee_active) {
        app->marquee_active = 0;
        return;
    }
    if (app->taskbar.menu_open) {
        app->taskbar.menu_open = 0;
        return;
    }
    if (app->data_editor.open) {
        app->data_editor.open = 0;
        return;
    }
    /* nothing transient left to back out of - lowest priority is dropping
       back to the idle Select tool (e.g. Input/Wire/Output was armed but
       never actually used to start dragging out a wire) and clearing
       whatever's selected on the canvas, together in the same press since
       neither is an "in-progress action" worth a separate Escape of its own. */
    app->active_tool = TOOL_SELECT;
    clear_selection(app);
}

/* Ctrl+C - copies a component and immediately starts a placement-at-cursor
   preview for it, same click-to-place interaction as the taskbar's Place
   tools but without a taskbar slot. Which component: whatever is directly
   under the cursor right now, even if nothing is selected - hovering alone
   is enough. Only if the cursor isn't over anything does it fall back to the
   current selection, and only if that selection is exactly one component -
   "eine markierte Komponente" is singular for a reason: which one would
   ambiguous multi-selection copy? Wires aren't copyable this way. */
static void copy_selected_component(App *app) {
    const Component *found = NULL;

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    if (my >= TASKBAR_HEIGHT) {
        int box_gx, box_gy;
        camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
        int comp_id = circuit_find_component_at(&app->circuit, box_gx, box_gy);
        if (comp_id >= 0) found = &app->circuit.components[comp_id];
    }

    if (found == NULL) {
        int ambiguous = 0;
        for (int i = 0; i < app->circuit.component_high_water; i++) {
            Component *c = &app->circuit.components[i];
            if (c->in_use && c->selected) {
                if (found != NULL) { ambiguous = 1; break; }
                found = c;
            }
        }
        if (ambiguous) found = NULL;
    }
    if (found == NULL) return;

    cancel_transient_actions(app); /* clean slate - drop any wiring/drag/marquee in progress first */
    app->clipboard_ic_def = found->ic_def;
    app->pasting = 1;
}

static void set_active_tool(App *app, Tool tool) {
    cancel_transient_actions(app);
    clear_selection(app); /* switching tools means we're moving on, drop any stale selection */
    app->active_tool = tool;
}

/* Returns 1 if the click was fully handled by the taskbar/dropdown (a tool
   or IC was chosen, or it just hit chrome like a category fold arrow), 0 if
   it missed everything and the caller should treat it as an ordinary canvas
   click instead (see taskbar_handle_click's comment on why a miss can still
   have closed an open dropdown as a side effect). */
static int handle_taskbar_click(App *app, int mx, int my) {
    Tool tool;
    const char *ic_name;
    TaskbarClickKind kind = taskbar_handle_click(&app->taskbar, mx, my, &tool, &ic_name);
    if (kind == TASKBAR_CLICK_TOOL) {
        set_active_tool(app, tool);
        return 1;
    }
    if (kind == TASKBAR_CLICK_IC) {
        set_active_tool(app, TOOL_PLACE_IC);
        app->place_ic_name = ic_name;
        return 1;
    }
    return kind == TASKBAR_CLICK_CONSUMED;
}

/* Mirrors handle_taskbar_click's contract: 1 if the click was fully handled
   by the Layers panel (a rename started, a swatch popup opened, a reorder/
   delete/add committed, ...), 0 if it missed and the caller should fall
   through to the canvas. double_click gates starting a rename - a plain
   click on a name just selects that layer (see layer_panel_handle_click). */
static int handle_layer_panel_click(App *app, int mx, int my, int double_click) {
    LayerPanelClickResult result = layer_panel_handle_click(&app->layer_panel, &app->circuit,
                                                              &app->active_layer_slot, mx, my, double_click);
    if (result == LAYER_PANEL_CLICK_CHANGED) undo_push(&app->circuit);
    return result != LAYER_PANEL_CLICK_MISS;
}

/* Which wire (if any) has an end at exactly (x,y) - used to find that
   node's own layer for TOOL_VIA placement below. If several wires meet
   there (same layer, or already via-bridged different layers), any one of
   them is an equally valid representative of "this node". */
/* A couple pixels of slack around the exact node point, same idea as
   TERMINAL_HIT_PADDING_PX below - reuses the existing wire hit tolerance so
   it stays consistent with wire-body picking. Thin wrapper over
   circuit_find_wire_node_near, which app.c's TOOL_VIA ghost preview also
   calls directly (it has no App-specific tolerance logic of its own to
   share, just the App-bound convenience of not having to compute the
   tolerance at every call site here). */
static int find_wire_node_at(App *app, float fx, float fy, int *out_x, int *out_y) {
    return circuit_find_wire_node_near(&app->circuit, fx, fy, app_wire_hit_tolerance(app), out_x, out_y);
}

/* TOOL_VIA: a via behaves like a tiny component pinned to a wire's node
   (endpoint/junction), never a free point - the click has to land on (or
   snap to, same tolerance as Select-mode node-dragging - see
   find_wire_node_at) an existing wire end. It then bridges that node's own
   layer with whichever layer is currently active (the Layers panel's
   highlighted row) - clicking with the active layer already matching does
   nothing, there's no such thing as a via bridging a layer to itself. */
static void handle_via_tool_click(App *app, float fx, float fy) {
    int node_x, node_y;
    if (!find_wire_node_at(app, fx, fy, &node_x, &node_y)) return;
    int wire_layer = circuit_wire_layer_at_point(&app->circuit, node_x, node_y);
    if (wire_layer < 0 || wire_layer == app->active_layer_slot) return;
    int new_id = circuit_add_via(&app->circuit, node_x, node_y, wire_layer, app->active_layer_slot);
    if (new_id >= 0) undo_push(&app->circuit);
}

static void handle_right_click(App *app, int mx, int my, float fx, float fy) {
    /* vias sit at exact grid vertices, same lattice-point matching pins
       use - checked first since a via is the most precise target that
       could be under the cursor, same precedence node-picking gets over
       body-picking in Select mode (see handle_left_click) */
    int gx, gy;
    camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
    int via_id = circuit_find_via_at(&app->circuit, gx, gy);
    if (via_id >= 0) {
        circuit_remove_via(&app->circuit, via_id);
        undo_push(&app->circuit);
        return;
    }

    /* box hit-test needs "which cell is the cursor over", not the nearest
       lattice point - see camera_screen_to_grid_floor */
    int box_gx, box_gy;
    camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
    int comp_id = circuit_find_component_at(&app->circuit, box_gx, box_gy);
    if (comp_id >= 0) {
        if (comp_id == app->selected_component_id) app->selected_component_id = -1;
        circuit_remove_component(&app->circuit, comp_id);
        undo_push(&app->circuit);
        return;
    }
    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        if (wire_id == app->selected_wire_id) app->selected_wire_id = -1;
        circuit_remove_wire(&app->circuit, wire_id);
        undo_push(&app->circuit);
    }
}

/* Snapshots which wire endpoints currently sit exactly on one of the given
   anchor points, so they can be dragged along too (see plan Revision 1: wires
   have no component/wire reference, so this must be simulated). Every anchor
   in a single drag moves by the same per-frame delta (components only
   translate, they don't rotate, and a dragged wire body moves rigidly), so
   there's no need to remember which anchor an attachment came from - see
   apply_drag_attachments below. exclude_wire_id (-1 for none) keeps a wire
   being body-dragged from attaching to itself. Already-.selected wires are
   also skipped - they're translated wholesale by the caller instead (see
   DRAG_SELECTION in app_handle_event), so picking them up here too would
   double-move them by the same per-frame delta. */
static void snapshot_drag_attachments(App *app, const GridPoint *anchors, int anchor_count, int exclude_wire_id) {
    app->drag_attach_count = 0;
    app->drag_attach_via_count = 0;
    for (int ai = 0; ai < anchor_count; ai++) {
        int px = anchors[ai].x, py = anchors[ai].y;
        for (int wi = 0; wi < app->circuit.wire_high_water && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; wi++) {
            if (wi == exclude_wire_id) continue;
            Wire *w = &app->circuit.wires[wi];
            if (!w->in_use || w->selected) continue;
            if (w->from_x == px && w->from_y == py) {
                app->drag_attach_wire_id[app->drag_attach_count] = wi;
                app->drag_attach_wire_end[app->drag_attach_count] = 0;
                app->drag_attach_count++;
            }
            if (app->drag_attach_count < MAX_DRAG_ATTACHMENTS && w->to_x == px && w->to_y == py) {
                app->drag_attach_wire_id[app->drag_attach_count] = wi;
                app->drag_attach_wire_end[app->drag_attach_count] = 1;
                app->drag_attach_count++;
            }
        }
        /* a via behaves like a tiny component pinned to this node - it
           moves along with whatever's being dragged too, same as an
           attached wire endpoint above */
        for (int vi = 0; vi < app->circuit.via_high_water && app->drag_attach_via_count < MAX_DRAG_ATTACHMENTS; vi++) {
            Via *v = &app->circuit.vias[vi];
            if (!v->in_use || v->x != px || v->y != py) continue;
            app->drag_attach_via_id[app->drag_attach_via_count] = vi;
            app->drag_attach_via_count++;
        }
    }
}

static void apply_drag_attachments(App *app, int dx, int dy) {
    for (int i = 0; i < app->drag_attach_count; i++) {
        Wire *w = &app->circuit.wires[app->drag_attach_wire_id[i]];
        if (app->drag_attach_wire_end[i] == 0) {
            w->from_x += dx;
            w->from_y += dy;
        } else {
            w->to_x += dx;
            w->to_y += dy;
        }
    }
    for (int i = 0; i < app->drag_attach_via_count; i++) {
        Via *v = &app->circuit.vias[app->drag_attach_via_id[i]];
        v->x += dx;
        v->y += dy;
    }
}

static void begin_component_drag(App *app, int comp_id, int gx, int gy) {
    select_component(app, comp_id);
    app->drag_kind = DRAG_COMPONENT;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
    Component *c = &app->circuit.components[comp_id];
    GridPoint anchors[MAX_PINS_PER_COMPONENT];
    for (int pi = 0; pi < c->pin_count; pi++) {
        component_pin_world_pos(c, pi, &anchors[pi].x, &anchors[pi].y);
    }
    snapshot_drag_attachments(app, anchors, c->pin_count, -1);
}

/* Moving a wire's body (grabbed away from either endpoint) translates the
   whole wire rigidly, dragging along anything attached at either end - same
   as moving a component. */
static void begin_wire_body_drag(App *app, int wire_id, int gx, int gy) {
    select_wire(app, wire_id);
    app->drag_kind = DRAG_WIRE_BODY;
    app->drag_wire_id = wire_id;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
    Wire *w = &app->circuit.wires[wire_id];
    GridPoint anchors[2] = { { w->from_x, w->from_y }, { w->to_x, w->to_y } };
    snapshot_drag_attachments(app, anchors, 2, wire_id);
}

static int selection_count(const App *app) {
    int n = 0;
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        if (app->circuit.components[i].in_use && app->circuit.components[i].selected) n++;
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        if (app->circuit.wires[i].in_use && app->circuit.wires[i].selected) n++;
    }
    return n;
}

/* Moving a multi-item selection (marquee or otherwise) drags every already-
   .selected component/wire together, rigidly, by the same per-frame delta -
   unlike begin_component_drag/begin_wire_body_drag, this does NOT touch the
   current selection first, since collapsing it down to just the clicked item
   is exactly the bug this exists to avoid. Anything unselected still
   attached to one of the selection's own anchor points (component pins, or
   a selected wire's own endpoints) is dragged along too, same as a single-
   item drag - see snapshot_drag_attachments. click_component_id/
   click_wire_id (one of them -1) is whichever single item was actually
   clicked to start this drag - see the drag_click_ and drag_moved fields'
   comment in app.h and finish_drag: a plain click with no movement still
   collapses down to just that one item. */
static void begin_selection_drag(App *app, int gx, int gy, int click_component_id, int click_wire_id) {
    app->drag_kind = DRAG_SELECTION;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_click_component_id = click_component_id;
    app->drag_click_wire_id = click_wire_id;
    app->drag_moved = 0;

    Circuit *circuit = &app->circuit;
    GridPoint anchors[MAX_DRAG_ATTACHMENTS];
    int anchor_count = 0;
    for (int i = 0; i < circuit->component_high_water && anchor_count < MAX_DRAG_ATTACHMENTS; i++) {
        Component *c = &circuit->components[i];
        if (!c->in_use || !c->selected) continue;
        for (int pi = 0; pi < c->pin_count && anchor_count < MAX_DRAG_ATTACHMENTS; pi++) {
            component_pin_world_pos(c, pi, &anchors[anchor_count].x, &anchors[anchor_count].y);
            anchor_count++;
        }
    }
    for (int i = 0; i < circuit->wire_high_water && anchor_count < MAX_DRAG_ATTACHMENTS; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use || !w->selected) continue;
        anchors[anchor_count].x = w->from_x;
        anchors[anchor_count].y = w->from_y;
        anchor_count++;
        if (anchor_count < MAX_DRAG_ATTACHMENTS) {
            anchors[anchor_count].x = w->to_x;
            anchors[anchor_count].y = w->to_y;
            anchor_count++;
        }
    }
    snapshot_drag_attachments(app, anchors, anchor_count, -1);
}

/* Moving a wire node grabs every wire endpoint exactly coincident with the
   grabbed point and moves them together - so dragging a junction keeps it a
   junction, instead of tearing one wire's end away from the others. They're
   marked .selected for the duration so the node being moved is visible. */
static void begin_wire_node_drag(App *app, int node_x, int node_y, int gx, int gy) {
    clear_selection(app);
    app->drag_kind = DRAG_WIRE_NODE;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
    app->drag_node_count = 0;
    app->drag_node_via_count = 0;
    Circuit *circuit = &app->circuit;
    for (int wi = 0; wi < circuit->wire_high_water && app->drag_node_count < MAX_DRAG_ATTACHMENTS; wi++) {
        Wire *w = &circuit->wires[wi];
        if (!w->in_use) continue;
        if (w->from_x == node_x && w->from_y == node_y) {
            app->drag_node_wire_id[app->drag_node_count] = wi;
            app->drag_node_wire_end[app->drag_node_count] = 0;
            app->drag_node_count++;
            w->selected = 1;
        }
        if (app->drag_node_count < MAX_DRAG_ATTACHMENTS && w->to_x == node_x && w->to_y == node_y) {
            app->drag_node_wire_id[app->drag_node_count] = wi;
            app->drag_node_wire_end[app->drag_node_count] = 1;
            app->drag_node_count++;
            w->selected = 1;
        }
    }
    /* any via pinned to this exact node moves along with it too */
    for (int vi = 0; vi < circuit->via_high_water && app->drag_node_via_count < MAX_DRAG_ATTACHMENTS; vi++) {
        Via *v = &circuit->vias[vi];
        if (v->in_use && v->x == node_x && v->y == node_y) {
            app->drag_node_via_id[app->drag_node_via_count] = vi;
            app->drag_node_via_count++;
        }
    }
}

static void begin_marquee_select(App *app, int mx, int my) {
    clear_selection(app);
    app->marquee_active = 1;
    app->marquee_start_mx = app->marquee_cur_mx = mx;
    app->marquee_start_my = app->marquee_cur_my = my;
}

/* Recomputes which components/wires are enclosed by the current marquee box
   and marks them .selected - called on every mouse-move during a marquee
   drag (see app_handle_event) so the selection previews live as the box
   grows/shrinks, instead of only appearing once the button is released.
   Fully enclosed only ("erst markiert, wenn vollständig markiert"), not
   merely touched - a box that only grazes a component's edge doesn't grab
   it. A wire counts as enclosed when both its endpoints are inside the box
   (it has no interior area of its own to test). Recomputed from scratch
   every call (clearing first) rather than incrementally, so shrinking the
   box correctly drops things it no longer covers. */
static void update_marquee_selection(App *app) {
    Circuit *circuit = &app->circuit;
    for (int i = 0; i < circuit->component_high_water; i++) circuit->components[i].selected = 0;
    for (int i = 0; i < circuit->wire_high_water; i++) circuit->wires[i].selected = 0;

    if (app->marquee_start_mx == app->marquee_cur_mx && app->marquee_start_my == app->marquee_cur_my) return;

    float gx0, gy0, gx1, gy1;
    camera_screen_to_grid_f(&app->camera, app->marquee_start_mx, app->marquee_start_my, &gx0, &gy0);
    camera_screen_to_grid_f(&app->camera, app->marquee_cur_mx, app->marquee_cur_my, &gx1, &gy1);
    float min_x = gx0 < gx1 ? gx0 : gx1, max_x = gx0 > gx1 ? gx0 : gx1;
    float min_y = gy0 < gy1 ? gy0 : gy1, max_y = gy0 > gy1 ? gy0 : gy1;

    for (int i = 0; i < circuit->component_high_water; i++) {
        Component *c = &circuit->components[i];
        if (!c->in_use) continue;
        int w, h;
        component_get_size(c, &w, &h);
        if (c->grid_x >= min_x && c->grid_x + w <= max_x && c->grid_y >= min_y && c->grid_y + h <= max_y) {
            c->selected = 1;
        }
    }
    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        if (w->from_x >= min_x && w->from_x <= max_x && w->from_y >= min_y && w->from_y <= max_y &&
            w->to_x >= min_x && w->to_x <= max_x && w->to_y >= min_y && w->to_y <= max_y) {
            w->selected = 1;
        }
    }
}

/* Button-up just ends the drag - the selection itself has already been kept
   live-updated by update_marquee_selection on every move (see above), so
   there's nothing left to compute here except one last catch-up call in case
   the final position never got a motion event of its own. */
static void finish_marquee_select(App *app) {
    app->marquee_active = 0;
    update_marquee_selection(app);
}

static WireKind tool_to_wire_kind(Tool tool) {
    if (tool == TOOL_INPUT) return WIRE_KIND_INPUT;
    if (tool == TOOL_OUTPUT) return WIRE_KIND_OUTPUT;
    return WIRE_KIND_NORMAL;
}

/* Input/Output are just wires with a kind tag - drawn by dragging exactly
   like the Wire tool (any length, any direction), not placed with a click. */
static void begin_wire_from(App *app, int gx, int gy, WireKind kind) {
    clear_selection(app); /* starting a wire drag is a new action, not a continuation of whatever was selected before */
    app->wiring = 1;
    app->wiring_kind = kind;
    app->wire_from_gx = gx;
    app->wire_from_gy = gy;
    app->wire_cursor_gx = gx;
    app->wire_cursor_gy = gy;
}

/* A couple pixels of slack around the exact glyph bounds - "kaum größer als
   die Buchstaben selbst". Reuses the exact same box render_wire_terminal
   draws into (render_wire_terminal_bounds), so the clickable area always
   matches what's on screen instead of an arbitrary radius around the wire's
   endpoint coordinate. */
#define TERMINAL_HIT_PADDING_PX 2

static int find_input_terminal_at(App *app, int mx, int my) {
    Circuit *circuit = &app->circuit;
    for (int i = 0; i < circuit->wire_high_water; i++) {
        Wire *w = &circuit->wires[i];
        if (!w->in_use || w->kind != WIRE_KIND_INPUT) continue;
        SDL_Rect b;
        if (!render_wire_terminal_bounds(app->font_large, &app->camera, w, circuit->wire_values[i], &b)) continue;
        if (mx >= b.x - TERMINAL_HIT_PADDING_PX && mx < b.x + b.w + TERMINAL_HIT_PADDING_PX &&
            my >= b.y - TERMINAL_HIT_PADDING_PX && my < b.y + b.h + TERMINAL_HIT_PADDING_PX) {
            return i;
        }
    }
    return -1;
}

static void handle_left_click(App *app, int mx, int my, int gx, int gy, float fx, float fy) {
    /* clicking an Input's H/L label always toggles it, no matter what tool is
       active or what else that click would otherwise do */
    int input_wire_id = find_input_terminal_at(app, mx, my);
    if (input_wire_id >= 0) {
        app->circuit.wires[input_wire_id].input_value = !app->circuit.wires[input_wire_id].input_value;
        return; /* toggling is not selecting - leave whatever was selected before untouched */
    }

    /* covers both an IC chosen from the Components menu (TOOL_PLACE_IC) and
       an in-progress Ctrl+C paste - app_pending_place_ic resolves which one
       wins (see its comment in app.h). Either way, stays in the current mode
       after placing (like Wire/Input/Output do) so several copies can be
       dropped in a row without reselecting the IC each time. */
    const IC_Def *place_def = app_pending_place_ic(app);
    if (place_def != NULL) {
        int w, h;
        ic_dip_body_size(place_def->pin_count, &w, &h);
        if (!circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1)) {
            int new_id = circuit_add_ic(&app->circuit, gx, gy, place_def);
            if (new_id >= 0) {
                select_component(app, new_id);
                undo_push(&app->circuit);
            }
        }
        return;
    }

    if (app->active_tool == TOOL_WIRE || app->active_tool == TOOL_INPUT || app->active_tool == TOOL_OUTPUT) {
        begin_wire_from(app, gx, gy, tool_to_wire_kind(app->active_tool));
        return;
    }

    /* TOOL_SELECT - wires are only ever started from the Wire/Input/Output tool
       now; Select mode does not let you drag a new wire off a pin, even by
       clicking one. It does let you drag a wire's node (every endpoint
       coincident at that point moves together), or the body of a wire/IC
       (moving it as a whole, dragging along anything attached at its
       endpoints/pins). Node-picking goes first since it's the most precise
       target and must win over a component/wire body sitting under it. */
    int node_x, node_y;
    if (find_wire_node_at(app, fx, fy, &node_x, &node_y)) {
        begin_wire_node_drag(app, node_x, node_y, gx, gy);
        return;
    }

    /* box hit-test needs "which cell is the cursor over", not the nearest
       lattice point (gx, gy) - see camera_screen_to_grid_floor */
    int box_gx, box_gy;
    camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
    int comp_id = circuit_find_component_at(&app->circuit, box_gx, box_gy);
    if (comp_id >= 0) {
        /* clicking a component that's already part of a multi-item selection
           drags the whole selection, instead of collapsing it down to just
           this one component (see begin_selection_drag) */
        if (app->circuit.components[comp_id].selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, comp_id, -1);
        } else {
            begin_component_drag(app, comp_id, gx, gy);
        }
        return;
    }

    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        if (app->circuit.wires[wire_id].selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, -1, wire_id);
        } else {
            begin_wire_body_drag(app, wire_id, gx, gy);
        }
        return;
    }

    /* nothing under the cursor - start a rubber-band selection box instead of
       just clearing the selection outright (a plain click with no drag still
       ends up clearing it, see finish_marquee_select) */
    begin_marquee_select(app, mx, my);
}

static void finish_wire(App *app, int gx, int gy) {
    app->wiring = 0;
    int new_id;
    if (app->wiring_kind == WIRE_KIND_NORMAL) {
        new_id = circuit_add_wire(&app->circuit, app->wire_from_gx, app->wire_from_gy, gx, gy, app->wiring_kind,
                                   app->active_layer_slot);
    } else {
        /* the H/L terminal renders at the wire's "from" end - Falstad drags
           AWAY from the pin/terminal, so that end is the release point, not
           where you first clicked down */
        new_id = circuit_add_wire(&app->circuit, gx, gy, app->wire_from_gx, app->wire_from_gy, app->wiring_kind,
                                   app->active_layer_slot);
    }
    if (new_id >= 0) undo_push(&app->circuit); /* -1 means from == to - no wire was actually added */
}

static void finish_drag(App *app) {
    /* a plain click-and-release on an item that was part of a bigger
       selection (no actual drag movement) collapses the selection down to
       just that one item - same as clicking any other unselected item
       always has. Only a real drag preserves and moves the whole group -
       see begin_selection_drag. */
    if (app->drag_kind == DRAG_SELECTION && !app->drag_moved) {
        if (app->drag_click_component_id >= 0) select_component(app, app->drag_click_component_id);
        else if (app->drag_click_wire_id >= 0) select_wire(app, app->drag_click_wire_id);
    }
    if (app->drag_moved) undo_push(&app->circuit);
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
}

/* A drag in progress (drag_wire_id, drag_attach_wire_id[], drag_node_wire_id[],
   selected_component_id, ...) holds indices into the CURRENT circuit -
   restoring a whole different Circuit snapshot underneath it could leave
   those pointing at an unrelated component/wire, or nothing at all, in the
   new one. Only cancels the drag itself (not wiring/marquee/paste/the
   active tool - those hold plain grid/screen coordinates or static
   registry pointers, nothing circuit-specific, so there's nothing unsafe
   about leaving them running through an undo/redo). Deliberately doesn't
   call undo_push - cancelling isn't itself an edit. */
static void cancel_drag_for_circuit_swap(App *app) {
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
}

static void perform_undo(App *app) {
    cancel_drag_for_circuit_swap(app);
    if (!undo_undo(&app->circuit)) return;
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
}

static void perform_redo(App *app) {
    cancel_drag_for_circuit_swap(app);
    if (!undo_redo(&app->circuit)) return;
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
}

void app_handle_event(App *app, const SDL_Event *event) {
    switch (event->type) {
        case SDL_QUIT:
            app->running = 0;
            break;

        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event->window.event == SDL_WINDOWEVENT_RESIZED) {
                app->window_w = event->window.data1;
                app->window_h = event->window.data2;
                taskbar_layout(&app->taskbar, app->window_w);
            }
            break;

        case SDL_MOUSEWHEEL: {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            if (!data_editor_handle_wheel(&app->data_editor, mx, my, event->wheel.y)) {
                camera_zoom_at(&app->camera, mx, my, event->wheel.y);
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN: {
            int mx = event->button.x, my = event->button.y;
            /* mouse "back"/"forward" side buttons (X1/X2 - e.g. a Razer
               DeathAdder's two thumb buttons) act as undo/redo globally,
               same as the keyboard shortcuts - not gated by taskbar/panel
               bounds like a left click is, since there's no "click through
               to the canvas" fallback meaning for these buttons anyway. */
            if (event->button.button == SDL_BUTTON_X1) {
                perform_undo(app);
                break;
            }
            if (event->button.button == SDL_BUTTON_X2) {
                perform_redo(app);
                break;
            }
            /* the Components dropdown can extend below TASKBAR_HEIGHT while
               open, so a plain y-cutoff isn't enough to gate taskbar clicks
               anymore - left clicks always go through the taskbar first
               (which knows its own bounds, including the dropdown, and
               reports back a miss so the click can fall through to the
               canvas); other buttons are just swallowed if they land on
               taskbar chrome, same as before. Manage Data's button/panel get
               the same treatment right after, for the same reason. */
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (handle_taskbar_click(app, mx, my)) break;
                if (data_editor_handle_click(&app->data_editor, data_editor_eligible(&app->circuit), mx, my)) break;
                if (handle_layer_panel_click(app, mx, my, event->button.clicks >= 2)) break;
            } else if (taskbar_covers_point(&app->taskbar, mx, my) ||
                       data_editor_covers_point(&app->data_editor, mx, my) ||
                       layer_panel_covers_point(&app->layer_panel, mx, my)) {
                break;
            }
            if (my < TASKBAR_HEIGHT) break; /* an empty gap in the taskbar strip itself */
            int gx, gy;
            float fx, fy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (app->active_tool == TOOL_VIA) handle_via_tool_click(app, fx, fy);
                else handle_left_click(app, mx, my, gx, gy, fx, fy);
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = 1;
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                handle_right_click(app, mx, my, fx, fy);
            }
            break;
        }

        case SDL_MOUSEBUTTONUP: {
            int mx = event->button.x, my = event->button.y;
            int gx, gy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            if (event->button.button == SDL_BUTTON_LEFT) {
                data_editor_handle_release(&app->data_editor);
                if (app->wiring) finish_wire(app, gx, gy);
                else if (app->drag_kind != DRAG_NONE) finish_drag(app);
                else if (app->marquee_active) finish_marquee_select(app);
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = 0;
            }
            break;
        }

        case SDL_MOUSEMOTION: {
            data_editor_handle_motion(&app->data_editor, event->motion.y);
            if (app->panning) {
                camera_pan(&app->camera, event->motion.xrel, event->motion.yrel);
            }
            if (app->wiring) {
                camera_screen_to_grid(&app->camera, event->motion.x, event->motion.y,
                                       &app->wire_cursor_gx, &app->wire_cursor_gy);
            }
            if (app->marquee_active) {
                app->marquee_cur_mx = event->motion.x;
                app->marquee_cur_my = event->motion.y;
                update_marquee_selection(app);
            }
            if (app->drag_kind != DRAG_NONE) {
                int gx, gy;
                camera_screen_to_grid(&app->camera, event->motion.x, event->motion.y, &gx, &gy);
                int dx = gx - app->drag_last_gx;
                int dy = gy - app->drag_last_gy;
                if (dx != 0 || dy != 0) {
                    app->drag_last_gx = gx;
                    app->drag_last_gy = gy;
                    app->drag_moved = 1;

                    if (app->drag_kind == DRAG_COMPONENT) {
                        Component *c = &app->circuit.components[app->selected_component_id];
                        c->grid_x += dx;
                        c->grid_y += dy;
                        apply_drag_attachments(app, dx, dy);
                    } else if (app->drag_kind == DRAG_WIRE_BODY) {
                        Wire *w = &app->circuit.wires[app->drag_wire_id];
                        w->from_x += dx;
                        w->from_y += dy;
                        w->to_x += dx;
                        w->to_y += dy;
                        apply_drag_attachments(app, dx, dy);
                    } else if (app->drag_kind == DRAG_WIRE_NODE) {
                        for (int i = 0; i < app->drag_node_count; i++) {
                            Wire *w = &app->circuit.wires[app->drag_node_wire_id[i]];
                            if (app->drag_node_wire_end[i] == 0) {
                                w->from_x += dx;
                                w->from_y += dy;
                            } else {
                                w->to_x += dx;
                                w->to_y += dy;
                            }
                        }
                        for (int i = 0; i < app->drag_node_via_count; i++) {
                            Via *v = &app->circuit.vias[app->drag_node_via_id[i]];
                            v->x += dx;
                            v->y += dy;
                        }
                    } else { /* DRAG_SELECTION */
                        for (int i = 0; i < app->circuit.component_high_water; i++) {
                            Component *c = &app->circuit.components[i];
                            if (!c->in_use || !c->selected) continue;
                            c->grid_x += dx;
                            c->grid_y += dy;
                        }
                        for (int i = 0; i < app->circuit.wire_high_water; i++) {
                            Wire *w = &app->circuit.wires[i];
                            if (!w->in_use || !w->selected) continue;
                            w->from_x += dx;
                            w->from_y += dy;
                            w->to_x += dx;
                            w->to_y += dy;
                        }
                        apply_drag_attachments(app, dx, dy);
                    }
                    circuit_rebuild_nets(&app->circuit);
                }
            }
            break;
        }

        case SDL_KEYDOWN: {
            SDL_Scancode sc = event->key.keysym.scancode;

            /* while renaming/adding a layer, the text-input widget owns
               every key - typing "w"/"v"/a digit must not also switch tools
               or the active layer out from under the field being edited */
            if (layer_panel_is_editing(&app->layer_panel)) {
                LayerPanelClickResult result = layer_panel_handle_key(&app->layer_panel, &app->circuit, sc);
                if (result == LAYER_PANEL_CLICK_CHANGED) undo_push(&app->circuit);
                break;
            }

            if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
                delete_selection(app);
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                handle_escape(app);
            } else if (sc == SDL_SCANCODE_W) {
                set_active_tool(app, TOOL_WIRE);
            } else if (sc == SDL_SCANCODE_V) {
                set_active_tool(app, TOOL_VIA);
            } else if (sc == SDL_SCANCODE_SPACE) {
                set_active_tool(app, TOOL_SELECT);
            } else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
                /* picks which layer new wires route on - a plain field
                   assignment, not structural, so no undo_push (see
                   circuit_add_wire's layer_slot param) */
                int pos = sc - SDL_SCANCODE_1;
                if (pos < app->circuit.layer_order_count) {
                    app->active_layer_slot = app->circuit.layer_order[pos];
                }
            } else if (sc == SDL_SCANCODE_C && (event->key.keysym.mod & KMOD_CTRL)) {
                copy_selected_component(app);
            } else if (sc == SDL_SCANCODE_Z && (event->key.keysym.mod & KMOD_CTRL)) {
                if (event->key.keysym.mod & KMOD_SHIFT) perform_redo(app); /* Ctrl+Shift+Z */
                else perform_undo(app);
            } else if (sc == SDL_SCANCODE_Y && (event->key.keysym.mod & KMOD_CTRL)) {
                perform_redo(app);
            } else if ((sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) && !event->key.repeat) {
                app->shift_held = 1;
                if (event->key.keysym.mod & KMOD_CTRL) {
                    /* Ctrl already down - this Shift press is a Ctrl+Shift
                       chord, toggling the lock on/off (CapsLock-like) */
                    app->layer_preview_locked = !app->layer_preview_locked;
                    app->shift_press_was_chord = 1;
                }
            } else if ((sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL) && !event->key.repeat) {
                if (app->shift_held && !app->shift_press_was_chord) {
                    /* Shift was already down when Ctrl arrived - same chord,
                       just pressed in the other order */
                    app->layer_preview_locked = !app->layer_preview_locked;
                    app->shift_press_was_chord = 1;
                }
            }
            break;
        }

        case SDL_KEYUP: {
            SDL_Scancode sc = event->key.keysym.scancode;
            if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
                app->shift_held = 0;
                /* a lone Shift tap/hold (never chorded with Ctrl) toggles
                   an active lock back off on release; a chorded release
                   leaves the lock as Ctrl+Shift set it */
                if (!app->shift_press_was_chord && app->layer_preview_locked) {
                    app->layer_preview_locked = 0;
                }
                app->shift_press_was_chord = 0;
            }
            break;
        }

        case SDL_TEXTINPUT:
            if (layer_panel_is_editing(&app->layer_panel)) {
                layer_panel_text_input(&app->layer_panel, event->text.text);
            }
            break;

        default:
            break;
    }
}
