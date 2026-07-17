#include "app.h"
#include "render.h"
#include "sim.h"
#include "text_util.h"
#include "ic_registry.h"

void app_init(App *app, int window_w, int window_h) {
    circuit_init(&app->circuit);
    camera_init(&app->camera);
    taskbar_init(&app->taskbar);
    taskbar_layout(&app->taskbar, window_w);
    app->font = text_util_load_font(14);
    /* loaded much larger than its display size (see label_scale in render.c)
       so zoomed-in IC pin labels stay crisp instead of blurring from bitmap
       upscaling - only used for those, everything else uses the regular font */
    app->font_large = text_util_load_font(48);

    app->active_tool = TOOL_SELECT;
    app->window_w = window_w;
    app->window_h = window_h;
    app->running = 1;

    app->selected_component_id = -1;
    app->selected_wire_id = -1;

    app->drag_kind = DRAG_NONE;
    app->drag_last_gx = 0;
    app->drag_last_gy = 0;
    app->drag_wire_id = -1;
    app->drag_attach_count = 0;
    app->drag_node_count = 0;

    app->panning = 0;

    app->marquee_active = 0;
    app->marquee_start_mx = 0;
    app->marquee_start_my = 0;
    app->marquee_cur_mx = 0;
    app->marquee_cur_my = 0;

    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    app->wire_from_gx = 0;
    app->wire_from_gy = 0;
    app->wire_cursor_gx = 0;
    app->wire_cursor_gy = 0;
}

void app_shutdown(App *app) {
    taskbar_shutdown(&app->taskbar);
    if (app->font != NULL) {
        TTF_CloseFont(app->font);
        app->font = NULL;
    }
    if (app->font_large != NULL) {
        TTF_CloseFont(app->font_large);
        app->font_large = NULL;
    }
}

void app_update(App *app) {
    sim_step(&app->circuit);
}

float app_wire_hit_tolerance(const App *app) {
    return WIRE_HIT_TOLERANCE_PX / camera_cell_px(&app->camera);
}

const char *app_place_tool_ic_name(Tool tool) {
    if (tool == TOOL_PLACE_SN7408) return "SN7408";
    if (tool == TOOL_PLACE_SN7432) return "SN7432";
    return NULL;
}

void app_get_tool_footprint(Tool tool, int *out_w, int *out_h) {
    const char *ic_name = app_place_tool_ic_name(tool);
    const IC_Def *def = (ic_name != NULL) ? ic_registry_get(ic_name) : NULL;
    if (def != NULL) {
        ic_dip_body_size(def->pin_count, out_w, out_h);
        return;
    }
    *out_w = 1;
    *out_h = 1;
}

/* Whatever sits exactly at (gx, gy) - a pin (-> its IC) or an existing wire -
   gets a temporary highlight, without touching persistent selection state. */
static void find_snap_target_at(App *app, int gx, int gy, int *out_component_id, int *out_wire_id) {
    *out_component_id = -1;
    *out_wire_id = -1;

    int pin_comp, pin_index;
    if (circuit_find_pin_at(&app->circuit, gx, gy, &pin_comp, &pin_index)) {
        *out_component_id = pin_comp;
        return;
    }
    int wid = circuit_find_wire_at(&app->circuit, (float)gx, (float)gy, app_wire_hit_tolerance(app));
    if (wid >= 0) *out_wire_id = wid;
}

/* Whatever the mouse is over - an IC body or a wire - gets the same temporary
   highlight, regardless of which tool is active, so hovering always previews
   what's underneath the cursor. */
static void find_hover_target_at(App *app, int mx, int my, int *out_component_id, int *out_wire_id) {
    *out_component_id = -1;
    *out_wire_id = -1;

    /* box hit-test needs "which cell is the cursor over", not the nearest
       lattice point - see camera_screen_to_grid_floor */
    int box_gx, box_gy;
    float fx, fy;
    camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
    camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);

    int comp_id = circuit_find_component_at(&app->circuit, box_gx, box_gy);
    if (comp_id >= 0) {
        *out_component_id = comp_id;
        return;
    }
    int wid = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wid >= 0) *out_wire_id = wid;
}

void app_render(App *app, SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
    SDL_RenderClear(renderer);

    /* while dragging out a wire, highlight both what it's anchored to (fixed
       for the whole drag) and whatever the cursor is currently hovering, so
       the start point stays visibly marked the entire time, not just briefly.
       Otherwise (any tool, as long as nothing exclusive is already using the
       highlight - an active wire drag or a Select-mode drag), highlight
       whatever the mouse is simply hovering over. */
    int snap_component_a = -1, snap_wire_a = -1;
    int snap_component_b = -1, snap_wire_b = -1;
    if (app->wiring) {
        find_snap_target_at(app, app->wire_from_gx, app->wire_from_gy, &snap_component_a, &snap_wire_a);
        find_snap_target_at(app, app->wire_cursor_gx, app->wire_cursor_gy, &snap_component_b, &snap_wire_b);
    } else if (app->drag_kind == DRAG_NONE && !app->marquee_active) {
        /* suppressed during a marquee drag - the box itself is the thing the
           user is focused on, and a hover highlight flickering underneath it
           as the cursor crosses components would just be confusing noise */
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (my >= TASKBAR_HEIGHT) {
            find_hover_target_at(app, mx, my, &snap_component_a, &snap_wire_a);
        }
    }

    render_grid(renderer, &app->camera, app->window_w, app->window_h);
    render_circuit(renderer, app->font_large, &app->circuit, &app->camera,
                    snap_component_a, snap_wire_a, snap_component_b, snap_wire_b);

    if (app->wiring) {
        render_wire_preview(renderer, &app->camera, app->wire_from_gx, app->wire_from_gy,
                             app->wire_cursor_gx, app->wire_cursor_gy);
    }

    if (app_place_tool_ic_name(app->active_tool) != NULL) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (my >= TASKBAR_HEIGHT) {
            int gx, gy, w, h;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            app_get_tool_footprint(app->active_tool, &w, &h);
            int valid = !circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1);
            render_placement_preview(renderer, &app->camera, gx, gy, w, h, valid);
        }
    }

    if (app->marquee_active) {
        render_marquee_select(renderer, app->marquee_start_mx, app->marquee_start_my,
                               app->marquee_cur_mx, app->marquee_cur_my);
    }

    taskbar_render(renderer, app->font, &app->taskbar, app->active_tool);
}
