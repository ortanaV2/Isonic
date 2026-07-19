#include "app.h"
#include "render.h"
#include "sim.h"
#include "text_util.h"
#include "ic_registry.h"
#include "diagnostics.h"
#include "undo.h"

void app_init(App *app, int window_w, int window_h) {
    circuit_init(&app->circuit);
    camera_init(&app->camera);
    taskbar_init(&app->taskbar);
    taskbar_layout(&app->taskbar, window_w);
    app->font = text_util_load_font(14);
    /* loaded much larger than its display size (see label_scale in render.c)
       so zoomed-in IC pin labels stay crisp instead of blurring from bitmap
       upscaling - only used for those, everything else uses the regular font */
    app->font_large = text_util_load_font(96); /* keep in sync with LABEL_FONT_POINT_SIZE in render.c */

    app->active_tool = TOOL_SELECT;
    app->place_ic_name = NULL;
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

    app->clipboard_ic_def = NULL;
    app->pasting = 0;

    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    app->wire_from_gx = 0;
    app->wire_from_gy = 0;
    app->wire_cursor_gx = 0;
    app->wire_cursor_gy = 0;

    app->diagnostics.count = 0;

    data_editor_init(&app->data_editor);

    /* baseline snapshot so the very first structural edit has something to
       undo back to (the empty starting circuit) - see undo.h */
    undo_init();
    undo_push(&app->circuit);
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
    /* after sim_step so PIN_OUTPUT values (tri-stated or not) are settled -
       see diagnostics_compute's comment on why that matters for telling a
       real driver from a SIG_HIZ one */
    diagnostics_compute(&app->circuit, &app->diagnostics);
}

float app_wire_hit_tolerance(const App *app) {
    return WIRE_HIT_TOLERANCE_PX / camera_cell_px(&app->camera);
}

const IC_Def *app_pending_place_ic(const App *app) {
    if (app->pasting && app->clipboard_ic_def != NULL) return app->clipboard_ic_def;
    if (app->active_tool == TOOL_PLACE_IC && app->place_ic_name != NULL) {
        return ic_registry_get(app->place_ic_name);
    }
    return NULL;
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

/* Whichever diagnostic (if any) references the pin or wire directly under
   the cursor - checked pin-first since a pin sits exactly on top of its own
   wire endpoint, and the pin is the more precise target of the two (see
   find_hover_target_at above for the same precedence on plain hover). Lets
   hovering a flagged spot on the canvas show its tooltip, same as hovering
   its chip in the bottom-left panel. */
static int find_diagnostic_hover_at(App *app, int mx, int my) {
    if (my < TASKBAR_HEIGHT) return -1;

    /* pins sit at exact grid vertices, matched by exact equality (see
       component_find_pin_at) - needs the nearest-lattice-point conversion,
       not the floored-cell one used for AREA/box hit-testing */
    int gx, gy;
    float fx, fy;
    camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
    camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);

    int pin_comp, pin_index;
    if (circuit_find_pin_at(&app->circuit, gx, gy, &pin_comp, &pin_index)) {
        for (int i = 0; i < app->diagnostics.count; i++) {
            if (diagnostic_has_pin(&app->diagnostics.items[i], pin_comp, pin_index)) return i;
        }
    }

    int wid = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wid >= 0) {
        for (int i = 0; i < app->diagnostics.count; i++) {
            if (diagnostic_has_wire(&app->diagnostics.items[i], wid)) return i;
        }
    }
    return -1;
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
    render_circuit(renderer, app->font_large, &app->circuit, &app->camera, &app->diagnostics,
                    snap_component_a, snap_wire_a, snap_component_b, snap_wire_b);
    render_diagnostic_highlights(renderer, &app->camera, &app->circuit, &app->diagnostics);

    if (app->wiring) {
        render_wire_preview(renderer, &app->camera, app->wire_from_gx, app->wire_from_gy,
                             app->wire_cursor_gx, app->wire_cursor_gy);
    }

    const IC_Def *pending_ic = app_pending_place_ic(app);
    if (pending_ic != NULL) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        /* suppressed while the cursor is over the taskbar strip, the (if
           open) Components dropdown, or the (if open) Manage Data panel -
           seeing the placement ghost peek out from behind either looks like
           hovering it might place a part, which it doesn't (see
           taskbar_covers_point/data_editor_covers_point) */
        if (!taskbar_covers_point(&app->taskbar, mx, my) && !data_editor_covers_point(&app->data_editor, mx, my)) {
            int gx, gy, w, h;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            ic_dip_body_size(pending_ic->pin_count, &w, &h);
            int valid = !circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1);
            render_placement_preview(renderer, &app->camera, gx, gy, w, h, valid);
        }
    }

    if (app->marquee_active) {
        render_marquee_select(renderer, app->marquee_start_mx, app->marquee_start_my,
                               app->marquee_cur_mx, app->marquee_cur_my);
    }

    int hover_mx, hover_my;
    SDL_GetMouseState(&hover_mx, &hover_my);
    /* place_ic_name itself is deliberately never cleared just for switching
       tools (see app.h - it's what lets reopening the Components dropdown or
       placing several copies in a row remember the last IC without
       reselecting it), so the dropdown's row highlight has to gate on
       active_tool itself instead of just checking place_ic_name != NULL -
       otherwise it stays highlighted forever after switching to Select/
       Wire/Input, well past the place-mode it was actually meant to show. */
    const char *highlighted_ic = (app->active_tool == TOOL_PLACE_IC) ? app->place_ic_name : NULL;
    taskbar_render(renderer, app->font, &app->taskbar, app->active_tool, highlighted_ic, hover_mx, hover_my);
    data_editor_render(renderer, app->font, &app->data_editor, data_editor_eligible(&app->circuit), &app->taskbar,
                        app->window_w, app->window_h, hover_mx, hover_my);

    /* the bottom-left chip stack always renders on top of everything else;
       a hovered chip wins over a hovered canvas target if somehow both are
       true at once (they never overlap in practice - the panel sits over
       empty space at the bottom-left corner) */
    int panel_hover = render_diagnostics_panel(renderer, app->font, app->window_h, &app->diagnostics, hover_mx, hover_my);
    int hovered_diag = (panel_hover >= 0) ? panel_hover : find_diagnostic_hover_at(app, hover_mx, hover_my);
    if (hovered_diag >= 0) {
        render_diagnostic_tooltip(renderer, app->font, &app->diagnostics.items[hovered_diag],
                                   hover_mx, hover_my, app->window_w, app->window_h);
    }
}
