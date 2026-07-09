#include "app.h"
#include "render.h"

static void clear_selection(App *app) {
    if (app->selected_component_id >= 0) {
        app->circuit.components[app->selected_component_id].selected = 0;
    }
    if (app->selected_wire_id >= 0) {
        app->circuit.wires[app->selected_wire_id].selected = 0;
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

static void delete_selection(App *app) {
    if (app->selected_component_id >= 0) {
        circuit_remove_component(&app->circuit, app->selected_component_id);
        app->selected_component_id = -1;
    } else if (app->selected_wire_id >= 0) {
        circuit_remove_wire(&app->circuit, app->selected_wire_id);
        app->selected_wire_id = -1;
    }
}

static void cancel_transient_actions(App *app) {
    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    app->dragging = 0;
    app->drag_attach_count = 0;
    app->panning = 0;
    app->active_tool = TOOL_SELECT;
}

static void handle_taskbar_click(App *app, int mx, int my) {
    int tool = taskbar_hit_test(&app->taskbar, mx, my);
    if (tool >= 0) {
        cancel_transient_actions(app);
        clear_selection(app); /* switching tools means we're moving on, drop any stale selection */
        app->active_tool = (Tool)tool;
    }
}

static void handle_right_click(App *app, int gx, int gy, float fx, float fy) {
    int comp_id = circuit_find_component_at(&app->circuit, gx, gy);
    if (comp_id >= 0) {
        if (comp_id == app->selected_component_id) app->selected_component_id = -1;
        circuit_remove_component(&app->circuit, comp_id);
        return;
    }
    float tolerance = WIRE_HIT_TOLERANCE_PX / camera_cell_px(&app->camera);
    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, tolerance);
    if (wire_id >= 0) {
        if (wire_id == app->selected_wire_id) app->selected_wire_id = -1;
        circuit_remove_wire(&app->circuit, wire_id);
    }
}

/* Snapshots which wire endpoints currently sit exactly on one of the
   component's pin tips, so they can be dragged along with it (see plan
   Revision 1: wires have no component reference, so this must be simulated). */
static void snapshot_drag_attachments(App *app, int comp_id) {
    app->drag_attach_count = 0;
    Component *c = &app->circuit.components[comp_id];
    for (int pi = 0; pi < c->pin_count && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; pi++) {
        int px, py;
        component_pin_world_pos(c, pi, &px, &py);
        for (int wi = 0; wi < app->circuit.wire_high_water && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; wi++) {
            Wire *w = &app->circuit.wires[wi];
            if (!w->in_use) continue;
            if (w->from_x == px && w->from_y == py) {
                app->drag_attach_wire_id[app->drag_attach_count] = wi;
                app->drag_attach_wire_end[app->drag_attach_count] = 0;
                app->drag_attach_pin_index[app->drag_attach_count] = pi;
                app->drag_attach_count++;
            }
            if (app->drag_attach_count < MAX_DRAG_ATTACHMENTS && w->to_x == px && w->to_y == py) {
                app->drag_attach_wire_id[app->drag_attach_count] = wi;
                app->drag_attach_wire_end[app->drag_attach_count] = 1;
                app->drag_attach_pin_index[app->drag_attach_count] = pi;
                app->drag_attach_count++;
            }
        }
    }
}

static void begin_drag(App *app, int comp_id, int gx, int gy) {
    select_component(app, comp_id);
    app->dragging = 1;
    Component *c = &app->circuit.components[comp_id];
    app->drag_offset_x = gx - c->grid_x;
    app->drag_offset_y = gy - c->grid_y;
    snapshot_drag_attachments(app, comp_id);
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

    if (app->active_tool == TOOL_PLACE_SN7408) {
        int w, h;
        app_get_tool_footprint(app->active_tool, &w, &h);
        if (circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1)) {
            return; /* overlapping placement rejected, stay in the tool */
        }
        const IC_Def *def = ic_registry_get("SN7408");
        int new_id = (def != NULL) ? circuit_add_ic(&app->circuit, gx, gy, def) : -1;
        app->active_tool = TOOL_SELECT;
        if (new_id >= 0) select_component(app, new_id);
        return;
    }

    if (app->active_tool == TOOL_WIRE || app->active_tool == TOOL_INPUT || app->active_tool == TOOL_OUTPUT) {
        begin_wire_from(app, gx, gy, tool_to_wire_kind(app->active_tool));
        return;
    }

    /* TOOL_SELECT */
    float tolerance = WIRE_HIT_TOLERANCE_PX / camera_cell_px(&app->camera);

    /* wires are only ever started from the Wire/Input/Output tool now - Select
       mode does not let you drag a new wire off a pin, even by clicking one */

    int comp_id = circuit_find_component_at(&app->circuit, gx, gy);
    if (comp_id >= 0) {
        begin_drag(app, comp_id, gx, gy);
        return;
    }

    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, tolerance);
    if (wire_id >= 0) {
        select_wire(app, wire_id);
        return;
    }

    clear_selection(app);
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
    app->dragging = 0;
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
                handle_right_click(app, gx, gy, fx, fy);
            }
            break;
        }

        case SDL_MOUSEBUTTONUP: {
            int mx = event->button.x, my = event->button.y;
            int gx, gy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (app->wiring) finish_wire(app, gx, gy);
                else if (app->dragging) finish_drag(app);
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
            if (app->dragging && app->selected_component_id >= 0) {
                int gx, gy;
                camera_screen_to_grid(&app->camera, event->motion.x, event->motion.y, &gx, &gy);
                Component *c = &app->circuit.components[app->selected_component_id];
                c->grid_x = gx - app->drag_offset_x;
                c->grid_y = gy - app->drag_offset_y;

                for (int i = 0; i < app->drag_attach_count; i++) {
                    int px, py;
                    component_pin_world_pos(c, app->drag_attach_pin_index[i], &px, &py);
                    Wire *w = &app->circuit.wires[app->drag_attach_wire_id[i]];
                    if (app->drag_attach_wire_end[i] == 0) {
                        w->from_x = px;
                        w->from_y = py;
                    } else {
                        w->to_x = px;
                        w->to_y = py;
                    }
                }
                circuit_rebuild_nets(&app->circuit);
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
            }
            break;
        }

        default:
            break;
    }
}
