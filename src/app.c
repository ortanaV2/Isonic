#include <stdio.h>
#include <string.h>
#include "app.h"
#include "render.h"
#include "sim.h"
#include "text_util.h"
#include "ic_registry.h"
#include "diagnostics.h"
#include "undo.h"
#include "schematic_io.h"
#include "platform_win32.h"

/* Shared by app_init and the New Schematic/Open File paths - clears every
   piece of transient UI/edit state that either points into the circuit
   (selection, drag indices, data_editor.editing) or would otherwise carry
   over from whatever document was open a moment ago, and gives the (fresh
   or freshly-loaded) circuit its own undo baseline. Does NOT touch
   app->circuit or app->camera themselves - New Schematic and Open File need
   different things there (reset vs repopulate), so those are the caller's
   job. */
static void app_reset_transient_state(App *app) {
    app->active_tool = TOOL_SELECT;
    app->place_ic_name = NULL;
    app->place_rotation = 0;

    app->selected_component_id = -1;
    app->selected_wire_id = -1;
    app->selected_section_id = -1;
    app->selected_text_label_id = -1;

    app->drag_kind = DRAG_NONE;
    app->drag_last_gx = 0;
    app->drag_last_gy = 0;
    app->drag_wire_id = -1;
    app->drag_attach_count = 0;
    app->drag_attach_via_count = 0;
    app->drag_node_count = 0;
    app->drag_node_via_count = 0;
    app->drag_section_corner = -1;

    app->panning = 0;

    app->marquee_active = 0;
    app->marquee_start_mx = 0;
    app->marquee_start_my = 0;
    app->marquee_cur_mx = 0;
    app->marquee_cur_my = 0;

    app->section_dragging = 0;
    app->canvas_edit_kind = CANVAS_EDIT_NONE;
    app->canvas_edit_id = -1;
    app->canvas_edit_buf[0] = '\0';
    app->canvas_edit_len = 0;

    app->clipboard_component_count = 0;
    app->clipboard_wire_count = 0;
    app->clipboard_via_count = 0;
    app->clipboard_section_count = 0;
    app->clipboard_text_label_count = 0;
    app->pasting = 0;

    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    app->wire_from_gx = 0;
    app->wire_from_gy = 0;
    app->wire_cursor_gx = 0;
    app->wire_cursor_gy = 0;

    app->diagnostics.count = 0;

    data_editor_init(&app->data_editor);

    app->taskbar.menu_open = 0;
    app->taskbar.file_menu_open = 0;

    layer_panel_init(&app->layer_panel);
    app->active_layer_slot = app->circuit.layer_order[0];

    /* baseline snapshot so the very first structural edit has something to
       undo back to (the current circuit, empty or freshly loaded) - see
       undo.h. A stale undo history from whatever document was open before
       must never bleed into this one. */
    undo_init();
    undo_push(&app->circuit);
}

/* Builds "Isonic Developer x64 - <filename or Untitled><*if dirty>" and
   applies it - called after every dirty/current_file_path change so the
   titlebar always reflects what's actually open, and whether it's saved. */
static void app_update_window_title(App *app) {
    if (app->window == NULL) return;

    const char *name = "Untitled";
    if (app->current_file_path[0] != '\0') {
        const char *slash1 = strrchr(app->current_file_path, '\\');
        const char *slash2 = strrchr(app->current_file_path, '/');
        name = app->current_file_path;
        if (slash1 != NULL && slash1 + 1 > name) name = slash1 + 1;
        if (slash2 != NULL && slash2 + 1 > name) name = slash2 + 1;
    }

    char title[600];
    snprintf(title, sizeof(title), "Isonic Developer x64 - %s%s", name, app->dirty ? "*" : "");
    SDL_SetWindowTitle(app->window, title);
}

void app_init(App *app, int window_w, int window_h, SDL_Window *window) {
    app->window = window;
    circuit_init(&app->circuit);
    camera_init(&app->camera);
    taskbar_init(&app->taskbar);
    taskbar_layout(&app->taskbar, window_w);
    app->font = text_util_load_font(14);
    /* loaded much larger than its display size (see label_scale in render.c)
       so zoomed-in IC pin labels stay crisp instead of blurring from bitmap
       upscaling - only used for those, everything else uses the regular font */
    app->font_large = text_util_load_font(96); /* keep in sync with LABEL_FONT_POINT_SIZE in render.c */

    app->window_w = window_w;
    app->window_h = window_h;
    app->running = 1;

    app->shift_held = 0;
    app->layer_preview_locked = 0;
    app->shift_press_was_chord = 0;

    settings_load(&app->settings);
    settings_panel_init(&app->settings_panel, &app->settings);
    app->dirty = 0;
    app->current_file_path[0] = '\0';
    app->last_autosave_tick = SDL_GetTicks();

    app_reset_transient_state(app);
}

void app_new_schematic(App *app) {
    circuit_init(&app->circuit);
    camera_init(&app->camera);
    app_reset_transient_state(app);
    app->current_file_path[0] = '\0';
    app->dirty = 0;
    app_update_window_title(app);
}

void app_load_from_file(App *app, const char *path) {
    /* the default/fallback camera position - schematic_load overwrites it
       only if the file actually has a camera record (see schematic_io.h),
       so a file saved before that existed still resets to this default,
       exactly like before */
    camera_init(&app->camera);
    if (!schematic_load(&app->circuit, &app->camera, path)) {
        platform_show_error(app->window, "Could not open that file - it may not be a valid Isonic schematic.");
        return;
    }
    app_reset_transient_state(app);
    snprintf(app->current_file_path, sizeof(app->current_file_path), "%s", path);
    app->dirty = 0;
    app_update_window_title(app);
}

int app_save_current(App *app) {
    if (app->current_file_path[0] == '\0') return app_save_as(app);
    if (!schematic_save(&app->circuit, &app->camera, app->current_file_path)) {
        platform_show_error(app->window, "Could not save the file.");
        return 0;
    }
    app->dirty = 0;
    app_update_window_title(app);
    return 1;
}

int app_save_as(App *app) {
    char path[ISONIC_PATH_MAX];
    if (!platform_save_file_dialog(app->window, path, sizeof(path))) return 0;
    if (!schematic_save(&app->circuit, &app->camera, path)) {
        platform_show_error(app->window, "Could not save the file.");
        return 0;
    }
    snprintf(app->current_file_path, sizeof(app->current_file_path), "%s", path);
    app->dirty = 0;
    app_update_window_title(app);
    return 1;
}

int app_confirm_discard_if_dirty(App *app) {
    if (!app->dirty) return 1;
    PlatformDiscardChoice choice = platform_confirm_discard_changes(app->window);
    if (choice == PLATFORM_DISCARD_CANCEL) return 0;
    if (choice == PLATFORM_DISCARD_SAVE) {
        if (!app_save_current(app)) return 0;
    }
    return 1;
}

void app_close_window(App *app) {
    if (!app_confirm_discard_if_dirty(app)) return;
    app->running = 0;
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

    /* Autosave only ever writes to a document that already has a real file
       path - an untitled document is never silently Save-As'd or written to
       some hidden recovery file. */
    if (app->settings.autosave_minutes > 0 && app->dirty && app->current_file_path[0] != '\0') {
        Uint32 interval_ms = (Uint32)app->settings.autosave_minutes * 60000u;
        if (SDL_GetTicks() - app->last_autosave_tick >= interval_ms) {
            if (schematic_save(&app->circuit, &app->camera, app->current_file_path)) {
                app->dirty = 0;
                app_update_window_title(app);
            }
            app->last_autosave_tick = SDL_GetTicks();
        }
    }
}

float app_wire_hit_tolerance(const App *app) {
    return WIRE_HIT_TOLERANCE_PX / camera_cell_px(&app->camera);
}

int clipboard_is_single_ic(const App *app) {
    return app->clipboard_component_count == 1 && app->clipboard_wire_count == 0 &&
           app->clipboard_via_count == 0 && app->clipboard_section_count == 0 &&
           app->clipboard_text_label_count == 0;
}

int clipboard_is_empty(const App *app) {
    return app->clipboard_component_count == 0 && app->clipboard_wire_count == 0 &&
           app->clipboard_via_count == 0 && app->clipboard_section_count == 0 &&
           app->clipboard_text_label_count == 0;
}

const IC_Def *app_pending_place_ic(const App *app) {
    if (app->pasting && clipboard_is_single_ic(app)) return app->clipboard_components[0].ic_def;
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

/* Which via (if any) sits exactly under the cursor - vias always sit at
   exact grid vertices, same lattice-point matching circuit_find_pin_at
   uses, not the floored-cell/tolerance matching hover/wire-body hit-tests
   use elsewhere. */
static int find_via_hover_at(App *app, int mx, int my) {
    if (my < TASKBAR_HEIGHT) return -1;
    int gx, gy;
    camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
    return circuit_find_via_at(&app->circuit, gx, gy);
}

/* Ghost for any in-progress paste EXCEPT a lone copied IC (that case keeps
   its own existing single-footprint preview - see app_pending_place_ic and
   the pending_ic block below). Draws every copied item at its prospective
   final position - (gx,gy) plus that item's own stored offset, see
   ClipboardComponent/Wire/Section/TextLabel in app.h - reusing the same
   "preview" drawing calls the rest of the app already uses for a single
   not-yet-committed item of each kind, rather than the full-fidelity
   render_circuit path, so a multi-item ghost stays cheap and doesn't need
   its own copy of a live Circuit to render from. No overlap/validity
   checking here (unlike the single-IC footprint case) - committing a
   general paste always succeeds, same as pasting a Section/Text Label
   always has. */
static void render_paste_ghost(SDL_Renderer *renderer, App *app, int gx, int gy) {
    for (int i = 0; i < app->clipboard_component_count; i++) {
        const ClipboardComponent *cc = &app->clipboard_components[i];
        render_ic_ghost(renderer, app->font_large, &app->camera, cc->ic_def, gx + cc->dx, gy + cc->dy, cc->rotation, 1);
    }
    for (int i = 0; i < app->clipboard_wire_count; i++) {
        const ClipboardWire *cw = &app->clipboard_wires[i];
        render_wire_preview(renderer, &app->camera, gx + cw->from_dx, gy + cw->from_dy,
                             gx + cw->to_dx, gy + cw->to_dy);
    }
    for (int i = 0; i < app->clipboard_via_count; i++) {
        const ClipboardVia *cv = &app->clipboard_vias[i];
        render_via_placement_preview(renderer, &app->camera, gx + cv->dx, gy + cv->dy, 1);
    }
    for (int i = 0; i < app->clipboard_section_count; i++) {
        const ClipboardSection *cs = &app->clipboard_sections[i];
        render_section_preview(renderer, app->font_large, &app->camera, gx + cs->dx0, gy + cs->dy0,
                                gx + cs->dx1, gy + cs->dy1, cs->label, 1);
    }
    for (int i = 0; i < app->clipboard_text_label_count; i++) {
        const ClipboardTextLabel *ct = &app->clipboard_text_labels[i];
        render_text_label_preview(renderer, app->font_large, &app->camera, gx + ct->dx, gy + ct->dy, ct->text, 1);
    }
}

void app_render(App *app, SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
    SDL_RenderClear(renderer);

    int hover_mx, hover_my;
    SDL_GetMouseState(&hover_mx, &hover_my);

    /* While the Settings modal is open, the cursor must have zero effect on
       anything behind it - not just clicks (already gated in
       input_handler.c) but hover state too: no taskbar/data-editor/layer-
       panel/section-lock-icon lighting up, no diagnostic/via tooltip.
       Feeding every one of those calls an off-screen sentinel position
       achieves that in one place instead of a separate guard in each - the
       modal itself still gets the real position at the very end, below.
       Computed up front (not just before taskbar_render, like before
       Sections existed) since render_sections below - drawn early,
       intentionally behind wires/components, see its own call - needs it
       too. */
    int outside_hover_mx = hover_mx, outside_hover_my = hover_my;
    if (app->settings_panel.open) {
        outside_hover_mx = -1;
        outside_hover_my = -1;
    }

    /* while dragging out a wire, highlight both what it's anchored to (fixed
       for the whole drag) and whatever the cursor is currently hovering, so
       the start point stays visibly marked the entire time, not just briefly.
       Otherwise (any tool, as long as nothing exclusive is already using the
       highlight - an active wire drag or a Select-mode drag), highlight
       whatever the mouse is simply hovering over. */
    int snap_component_a = -1, snap_wire_a = -1;
    int snap_component_b = -1, snap_wire_b = -1;
    /* the Settings popup is a true modal - the cursor must have zero effect
       on the canvas while it's open, so no hover-highlight is computed at
       all in that case (same reasoning as the marquee-suppression case
       below, just for the whole block). */
    if (!app->settings_panel.open) {
        if (app->wiring) {
            find_snap_target_at(app, app->wire_from_gx, app->wire_from_gy, &snap_component_a, &snap_wire_a);
            find_snap_target_at(app, app->wire_cursor_gx, app->wire_cursor_gy, &snap_component_b, &snap_wire_b);
        } else if (app->drag_kind == DRAG_NONE && !app->marquee_active) {
            /* suppressed during a marquee drag - the box itself is the thing
               the user is focused on, and a hover highlight flickering
               underneath it as the cursor crosses components would just be
               confusing noise */
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            if (my >= TASKBAR_HEIGHT) {
                find_hover_target_at(app, mx, my, &snap_component_a, &snap_wire_a);
            }
        }
    }

    render_grid(renderer, &app->camera, app->window_w, app->window_h);

    /* Sections render early - as a background annotation layer, so wires/
       components drawn right after stay fully visible/clickable inside one
       instead of it obscuring anything (see circuit.h's Section comment).
       editing_section_id/canvas_edit_buf only apply while THIS section's
       label is being retyped (CANVAS_EDIT_SECTION_LABEL) - anything else
       (nothing being edited, or a brand new pending one, or a Text Label
       instead) leaves every committed section showing its own stored
       label untouched. */
    int editing_section_id = (app->canvas_edit_kind == CANVAS_EDIT_SECTION_LABEL) ? app->canvas_edit_id : -1;
    render_sections(renderer, app->font_large, &app->camera, &app->circuit, editing_section_id, app->canvas_edit_buf,
                     outside_hover_mx, outside_hover_my);

    int layer_preview = app->shift_held || app->layer_preview_locked;
    render_circuit(renderer, app->font_large, &app->circuit, &app->camera, &app->diagnostics, layer_preview,
                    snap_component_a, snap_wire_a, snap_component_b, snap_wire_b);
    render_diagnostic_highlights(renderer, &app->camera, &app->circuit, &app->diagnostics);

    /* Text Labels render on top of everything else in the canvas content -
       unlike a Section's background rectangle, a placed note is meant to
       stay legible even where a wire happens to cross behind it. */
    int editing_text_label_id = (app->canvas_edit_kind == CANVAS_EDIT_TEXT_LABEL) ? app->canvas_edit_id : -1;
    render_text_labels(renderer, app->font_large, &app->camera, &app->circuit, editing_text_label_id, app->canvas_edit_buf,
                        outside_hover_mx, outside_hover_my);

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
        if (!taskbar_covers_point(&app->taskbar, mx, my) && !data_editor_covers_point(&app->data_editor, mx, my) &&
            !settings_panel_covers_point(&app->settings_panel, mx, my)) {
            int gx, gy, w, h;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            ic_dip_body_size(pending_ic->pin_count, &w, &h);
            if (app->place_rotation & 1) { int t = w; w = h; h = t; }
            int valid = !circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1);
            render_ic_ghost(renderer, app->font_large, &app->camera, pending_ic, gx, gy, app->place_rotation, valid);
        }
    }

    if (app->pasting && !clipboard_is_single_ic(app)) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        /* same suppression the single-IC ghost above uses - see its own
           comment */
        if (!taskbar_covers_point(&app->taskbar, mx, my) && !data_editor_covers_point(&app->data_editor, mx, my) &&
            !settings_panel_covers_point(&app->settings_panel, mx, my) && my >= TASKBAR_HEIGHT) {
            int gx, gy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            render_paste_ghost(renderer, app, gx, gy);
        }
    }

    if (app->active_tool == TOOL_VIA) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (my >= TASKBAR_HEIGHT && !taskbar_covers_point(&app->taskbar, mx, my) &&
            !data_editor_covers_point(&app->data_editor, mx, my) &&
            !layer_panel_covers_point(&app->layer_panel, mx, my) &&
            !settings_panel_covers_point(&app->settings_panel, mx, my)) {
            float fx, fy;
            camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);
            int node_x, node_y;
            if (circuit_find_wire_node_near(&app->circuit, fx, fy, app_wire_hit_tolerance(app), &node_x, &node_y)) {
                int wire_layer = circuit_wire_layer_at_point(&app->circuit, node_x, node_y);
                int valid = (wire_layer >= 0 && wire_layer != app->active_layer_slot);
                render_via_placement_preview(renderer, &app->camera, node_x, node_y, valid);
            }
        }
    }

    if (app->marquee_active) {
        render_marquee_select(renderer, app->marquee_start_mx, app->marquee_start_my,
                               app->marquee_cur_mx, app->marquee_cur_my);
    }

    if (app->section_dragging) {
        /* both corners are already grid coordinates - see app.h's
           section_drag_start_gx/gy comment on why this used to reconvert
           stale screen pixels here every frame instead (and would visibly
           drift if the camera zoomed mid-drag) */
        render_section_preview(renderer, app->font_large, &app->camera, app->section_drag_start_gx,
                                app->section_drag_start_gy, app->section_drag_cur_gx, app->section_drag_cur_gy, "", 0);
    }
    if (app->canvas_edit_kind == CANVAS_EDIT_NEW_SECTION) {
        render_section_preview(renderer, app->font_large, &app->camera, app->pending_section_x0, app->pending_section_y0,
                                app->pending_section_x1, app->pending_section_y1, app->canvas_edit_buf, 0);
    } else if (app->canvas_edit_kind == CANVAS_EDIT_NEW_TEXT_LABEL) {
        render_text_label_preview(renderer, app->font_large, &app->camera, app->pending_text_label_x,
                                   app->pending_text_label_y, app->canvas_edit_buf, 0);
    }

    /* place_ic_name itself is deliberately never cleared just for switching
       tools (see app.h - it's what lets reopening the Components dropdown or
       placing several copies in a row remember the last IC without
       reselecting it), so the dropdown's row highlight has to gate on
       active_tool itself instead of just checking place_ic_name != NULL -
       otherwise it stays highlighted forever after switching to Select/
       Wire/Input, well past the place-mode it was actually meant to show. */
    const char *highlighted_ic = (app->active_tool == TOOL_PLACE_IC) ? app->place_ic_name : NULL;

    /* shift the Section-Labeling/Text Label button group over by the Manage
       Data button's real width, if it's showing this frame - must happen
       before taskbar_render below (and before data_editor_render further
       down, which computes the same width fresh off the same
       data_editor_eligible call, so the two never disagree about it). */
    Component *manage_data_eligible = data_editor_eligible(&app->circuit);
    int manage_data_w = data_editor_button_width(app->font, manage_data_eligible != NULL);
    taskbar_position_annotation_group(&app->taskbar, manage_data_w);

    taskbar_render(renderer, app->font, &app->taskbar, app->active_tool, outside_hover_mx, outside_hover_my);

    /* the bottom-left chip stack renders early now, BELOW the Manage Data
       and Layers panels (drawn right after) - it used to be the very last
       thing drawn and would sit on top of (and get hidden behind, depending
       on draw order at the time) whatever's docked on the right edge, which
       read as broken either way. A hovered chip still wins over a hovered
       canvas target below if somehow both are true at once (they never
       overlap in practice - the panel sits over empty space at the
       bottom-left corner).

       Chips still shouldn't be added past whatever's docked on the right
       edge even though they now render underneath it - stopping only at the
       window edge left them running full-length behind the Manage Data
       panel, invisible but still occupying that space (and still reachable
       by mouse hover, which would have been confusing). Clamp the available
       width to that panel's left edge when it's open, same one-frame-stale
       panel_rect read layer_panel_right_margin below already relies on. */
    int diag_max_x = app->data_editor.open ? app->data_editor.panel_rect.x : app->window_w;
    /* Settings' "Error/Warning Chips" toggle - off skips drawing the chip
       stack entirely, rather than just hiding it, so there's also nothing
       left there for the hover-tooltip lookup below to find. */
    int panel_hover = -1;
    if (app->settings.diag_chips_enabled) {
        panel_hover = render_diagnostics_panel(renderer, app->font, diag_max_x, app->window_h, &app->diagnostics,
                                                outside_hover_mx, outside_hover_my);
    }

    data_editor_render(renderer, app->font, &app->data_editor, manage_data_eligible, &app->taskbar,
                        app->window_w, app->window_h, outside_hover_mx, outside_hover_my);

    /* slides left to sit beside the Manage Data panel instead of
       overlapping it, when that's open */
    int layer_panel_right_margin = app->data_editor.open ? app->data_editor.panel_rect.w : 0;
    layer_panel_render(renderer, app->font, &app->layer_panel, &app->circuit, app->active_layer_slot,
                        app->window_w, app->window_h, layer_panel_right_margin, app->settings.layer_panel_anchor_left,
                        outside_hover_mx, outside_hover_my);

    /* Settings' "Error/Warning Hover Descriptions" toggle - independent of
       the chip toggle above, since a flagged wire/pin on the canvas itself
       can still be hovered for a description even with the chip stack
       turned off (render_diagnostic_highlights' colored dots aren't gated
       by either setting, only this text popup is). */
    int hovered_diag = -1;
    if (app->settings.diag_hover_enabled) {
        hovered_diag = (panel_hover >= 0) ? panel_hover : find_diagnostic_hover_at(app, outside_hover_mx, outside_hover_my);
    }
    if (hovered_diag >= 0) {
        render_diagnostic_tooltip(renderer, app->font, &app->diagnostics.items[hovered_diag],
                                   outside_hover_mx, outside_hover_my, app->window_w, app->window_h);
    } else {
        int hovered_via = find_via_hover_at(app, outside_hover_mx, outside_hover_my);
        if (hovered_via >= 0) {
            const Via *v = &app->circuit.vias[hovered_via];
            render_via_tooltip(renderer, app->font, app->circuit.layers[v->layer_slot_a].name,
                                app->circuit.layers[v->layer_slot_b].name, outside_hover_mx, outside_hover_my,
                                app->window_w, app->window_h);
        }
    }

    /* the File/Components dropdowns are the app's topmost layer short of the
       Settings modal - drawn last of the "ordinary" UI, after the Manage
       Data/Layers panels, so an open dropdown never ends up tucked behind
       either one (it used to, since taskbar_render drew them together with
       the rest of the taskbar bar, first). */
    taskbar_render_dropdowns(renderer, app->font, &app->taskbar, highlighted_ic, outside_hover_mx, outside_hover_my);

    /* topmost of all - a true modal, drawn last so its scrim covers
       absolutely everything else while open - this is the one call that
       gets the REAL cursor position, so the popup's own buttons stay
       interactive. */
    settings_panel_render(renderer, app->font, &app->settings_panel, app->window_w, app->window_h, hover_mx, hover_my);
}
