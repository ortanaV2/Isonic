#include "app.h"
#include "render.h"

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
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        if (app->circuit.components[i].in_use && app->circuit.components[i].selected) {
            circuit_remove_component(&app->circuit, i);
        }
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        if (app->circuit.wires[i].in_use && app->circuit.wires[i].selected) {
            circuit_remove_wire(&app->circuit, i);
        }
    }
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
}

/* Unmarks the wires temporarily highlighted for a DRAG_WIRE_NODE (see
   drag_node_* in app.h) - they aren't tracked by selected_wire_id, since
   several wires can share one node, so they need their own cleanup. */
static void clear_wire_node_marks(App *app) {
    for (int i = 0; i < app->drag_node_count; i++) {
        app->circuit.wires[app->drag_node_wire_id[i]].selected = 0;
    }
    app->drag_node_count = 0;
}

static void cancel_transient_actions(App *app) {
    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
    app->panning = 0;
    app->marquee_active = 0;
    app->active_tool = TOOL_SELECT;
}

static void set_active_tool(App *app, Tool tool) {
    cancel_transient_actions(app);
    clear_selection(app); /* switching tools means we're moving on, drop any stale selection */
    app->active_tool = tool;
}

static void handle_taskbar_click(App *app, int mx, int my) {
    int tool = taskbar_hit_test(&app->taskbar, mx, my);
    if (tool >= 0) {
        set_active_tool(app, (Tool)tool);
    }
}

static void handle_right_click(App *app, int mx, int my, float fx, float fy) {
    /* box hit-test needs "which cell is the cursor over", not the nearest
       lattice point - see camera_screen_to_grid_floor */
    int box_gx, box_gy;
    camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
    int comp_id = circuit_find_component_at(&app->circuit, box_gx, box_gy);
    if (comp_id >= 0) {
        if (comp_id == app->selected_component_id) app->selected_component_id = -1;
        circuit_remove_component(&app->circuit, comp_id);
        return;
    }
    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        if (wire_id == app->selected_wire_id) app->selected_wire_id = -1;
        circuit_remove_wire(&app->circuit, wire_id);
    }
}

/* Snapshots which wire endpoints currently sit exactly on one of the given
   anchor points, so they can be dragged along too (see plan Revision 1: wires
   have no component/wire reference, so this must be simulated). Every anchor
   in a single drag moves by the same per-frame delta (components only
   translate, they don't rotate, and a dragged wire body moves rigidly), so
   there's no need to remember which anchor an attachment came from - see
   apply_drag_attachments below. exclude_wire_id (-1 for none) keeps a wire
   being body-dragged from attaching to itself. */
static void snapshot_drag_attachments(App *app, const GridPoint *anchors, int anchor_count, int exclude_wire_id) {
    app->drag_attach_count = 0;
    for (int ai = 0; ai < anchor_count && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; ai++) {
        int px = anchors[ai].x, py = anchors[ai].y;
        for (int wi = 0; wi < app->circuit.wire_high_water && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; wi++) {
            if (wi == exclude_wire_id) continue;
            Wire *w = &app->circuit.wires[wi];
            if (!w->in_use) continue;
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
}

static void begin_component_drag(App *app, int comp_id, int gx, int gy) {
    select_component(app, comp_id);
    app->drag_kind = DRAG_COMPONENT;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
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
    Wire *w = &app->circuit.wires[wire_id];
    GridPoint anchors[2] = { { w->from_x, w->from_y }, { w->to_x, w->to_y } };
    snapshot_drag_attachments(app, anchors, 2, wire_id);
}

/* A couple pixels of slack around the exact node point, same idea as
   TERMINAL_HIT_PADDING_PX below - reuses the existing wire hit tolerance so
   it stays consistent with wire-body picking. */
static int find_wire_node_at(App *app, float fx, float fy, int *out_x, int *out_y) {
    float tol = app_wire_hit_tolerance(app);
    float best_d2 = tol * tol;
    int found = 0;
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        const Wire *w = &app->circuit.wires[i];
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

/* Moving a wire node grabs every wire endpoint exactly coincident with the
   grabbed point and moves them together - so dragging a junction keeps it a
   junction, instead of tearing one wire's end away from the others. They're
   marked .selected for the duration so the node being moved is visible. */
static void begin_wire_node_drag(App *app, int node_x, int node_y, int gx, int gy) {
    clear_selection(app);
    app->drag_kind = DRAG_WIRE_NODE;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_node_count = 0;
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

    const char *place_ic_name = app_place_tool_ic_name(app->active_tool);
    if (place_ic_name != NULL) {
        int w, h;
        app_get_tool_footprint(app->active_tool, &w, &h);
        if (circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1)) {
            return; /* overlapping placement rejected, stay in the tool */
        }
        const IC_Def *def = ic_registry_get(place_ic_name);
        int new_id = (def != NULL) ? circuit_add_ic(&app->circuit, gx, gy, def) : -1;
        app->active_tool = TOOL_SELECT;
        if (new_id >= 0) select_component(app, new_id);
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
        begin_component_drag(app, comp_id, gx, gy);
        return;
    }

    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        begin_wire_body_drag(app, wire_id, gx, gy);
        return;
    }

    /* nothing under the cursor - start a rubber-band selection box instead of
       just clearing the selection outright (a plain click with no drag still
       ends up clearing it, see finish_marquee_select) */
    begin_marquee_select(app, mx, my);
}

static void finish_wire(App *app, int gx, int gy) {
    app->wiring = 0;
    if (app->wiring_kind == WIRE_KIND_NORMAL) {
        circuit_add_wire(&app->circuit, app->wire_from_gx, app->wire_from_gy, gx, gy, app->wiring_kind);
    } else {
        /* the H/L terminal renders at the wire's "from" end - Falstad drags
           AWAY from the pin/terminal, so that end is the release point, not
           where you first clicked down */
        circuit_add_wire(&app->circuit, gx, gy, app->wire_from_gx, app->wire_from_gy, app->wiring_kind);
    }
}

static void finish_drag(App *app) {
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
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
            camera_zoom_at(&app->camera, mx, my, event->wheel.y);
            break;
        }

        case SDL_MOUSEBUTTONDOWN: {
            int mx = event->button.x, my = event->button.y;
            if (my < TASKBAR_HEIGHT) {
                if (event->button.button == SDL_BUTTON_LEFT) {
                    handle_taskbar_click(app, mx, my);
                }
                break;
            }
            int gx, gy;
            float fx, fy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);
            if (event->button.button == SDL_BUTTON_LEFT) {
                handle_left_click(app, mx, my, gx, gy, fx, fy);
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
                if (app->wiring) finish_wire(app, gx, gy);
                else if (app->drag_kind != DRAG_NONE) finish_drag(app);
                else if (app->marquee_active) finish_marquee_select(app);
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = 0;
            }
            break;
        }

        case SDL_MOUSEMOTION: {
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
                    } else { /* DRAG_WIRE_NODE */
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
                    }
                    circuit_rebuild_nets(&app->circuit);
                }
            }
            break;
        }

        case SDL_KEYDOWN: {
            SDL_Scancode sc = event->key.keysym.scancode;
            if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
                delete_selection(app);
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                cancel_transient_actions(app);
                clear_selection(app);
            } else if (sc == SDL_SCANCODE_W) {
                set_active_tool(app, TOOL_WIRE);
            } else if (sc == SDL_SCANCODE_SPACE) {
                set_active_tool(app, TOOL_SELECT);
            }
            break;
        }

        default:
            break;
    }
}
