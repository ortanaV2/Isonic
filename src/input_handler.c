#include <stdio.h>
#include <string.h>
#include "app.h"
#include "render.h"
#include "undo.h"
#include "platform_win32.h"

/* Every structural edit marks the document dirty (for the New/Open/Close
   discard-confirm prompt and autosave, see app.h) at exactly the same call
   sites this project's undo snapshots already use - "everything that would
   be lost by discarding" and "everything undo tracks" are the same set. */
static void push_undo(App *app) {
    undo_push(&app->circuit);
    app->dirty = 1;
}

/* Clears every .selected flag in the circuit, not just the tracked single
   ids - a marquee selection (see finish_marquee_select) can mark several
   components/wires/sections/text labels at once without ever touching
   selected_component_id/selected_wire_id/selected_section_id/
   selected_text_label_id, so those alone aren't enough to undo it. */
static void clear_selection(App *app) {
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        app->circuit.components[i].selected = 0;
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        app->circuit.wires[i].selected = 0;
    }
    for (int i = 0; i < app->circuit.section_high_water; i++) {
        app->circuit.sections[i].selected = 0;
    }
    for (int i = 0; i < app->circuit.text_label_high_water; i++) {
        app->circuit.text_labels[i].selected = 0;
    }
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
    app->selected_section_id = -1;
    app->selected_text_label_id = -1;
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

static void select_section(App *app, int id) {
    clear_selection(app);
    app->selected_section_id = id;
    app->circuit.sections[id].selected = 1;
}

static void select_text_label(App *app, int id) {
    clear_selection(app);
    app->selected_text_label_id = id;
    app->circuit.text_labels[id].selected = 1;
}

/* Removes every selected component/wire/section/text label, not just the
   single tracked ids - a marquee selection can mark several at once (see
   clear_selection above). A LOCKED section is skipped entirely - lock means
   "leave this alone", and Delete while multi-selecting nearby components is
   exactly the kind of accident that's meant to prevent (see circuit.h's
   Section comment) - it stays selected afterward rather than silently
   vanishing from the selection too. */
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
    for (int i = 0; i < app->circuit.section_high_water; i++) {
        Section *s = &app->circuit.sections[i];
        if (s->in_use && s->selected && !s->locked) {
            circuit_remove_section(&app->circuit, i);
            removed = 1;
        }
    }
    for (int i = 0; i < app->circuit.text_label_high_water; i++) {
        if (app->circuit.text_labels[i].in_use && app->circuit.text_labels[i].selected) {
            circuit_remove_text_label(&app->circuit, i);
            removed = 1;
        }
    }
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
    app->selected_section_id = -1;
    app->selected_text_label_id = -1;
    if (removed) push_undo(app);
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

/* Section labels and Text Labels have different max lengths (see circuit.h)
   even though they share one edit buffer - this is the cap SDL_TEXTINPUT
   routing enforces while typing, so it can never even momentarily hold more
   than whichever kind is actually being edited will keep at commit time. */
static int canvas_edit_max_len(CanvasEditKind kind) {
    if (kind == CANVAS_EDIT_NEW_SECTION || kind == CANVAS_EDIT_SECTION_LABEL) return SECTION_LABEL_MAX_LEN;
    return TEXT_LABEL_MAX_LEN;
}

/* Discards (never commits) any canvas text edit in progress - used both by
   Escape and by cancel_transient_actions below, which must never leave a
   stale SDL_StartTextInput() active behind a tool switch. */
static void cancel_canvas_edit(App *app) {
    if (app->canvas_edit_kind == CANVAS_EDIT_NONE) return;
    app->canvas_edit_kind = CANVAS_EDIT_NONE;
    app->canvas_edit_id = -1;
    SDL_StopTextInput();
}

/* Enter, or a click away from the field being typed - commits a non-empty
   buffer (adding the pending Section/Text Label to the circuit for the
   first time, or overwriting an existing one's label/text), same as
   layer_panel.c's own rename field. An EMPTY commit on an EXISTING
   section/label leaves it unchanged rather than blanking it (same as
   layer_panel.c); an empty commit on a brand new one just discards the
   pending geometry - nothing is ever added with blank text. */
static void commit_canvas_edit(App *app) {
    if (app->canvas_edit_kind == CANVAS_EDIT_NONE) return;
    if (app->canvas_edit_len > 0) {
        switch (app->canvas_edit_kind) {
            case CANVAS_EDIT_NEW_SECTION: {
                int id = circuit_add_section(&app->circuit, app->pending_section_x0, app->pending_section_y0,
                                              app->pending_section_x1, app->pending_section_y1, app->canvas_edit_buf);
                if (id >= 0) push_undo(app);
                break;
            }
            case CANVAS_EDIT_SECTION_LABEL: {
                /* canvas_edit_buf is sized for the larger of the two kinds
                   (CANVAS_EDIT_BUF_LEN, see app.h) - typing itself already
                   caps a Section label at SECTION_LABEL_MAX_LEN
                   (canvas_edit_max_len), so this can never actually
                   truncate, but gcc can't see that across the two
                   functions and flags it as -Wformat-truncation regardless.
                   An explicit precision matching the destination makes the
                   bound visible to the compiler instead of just relying on
                   snprintf's own (already-safe) runtime truncation. */
                Section *s = &app->circuit.sections[app->canvas_edit_id];
                snprintf(s->label, sizeof(s->label), "%.*s", (int)sizeof(s->label) - 1, app->canvas_edit_buf);
                push_undo(app);
                break;
            }
            case CANVAS_EDIT_NEW_TEXT_LABEL: {
                int id = circuit_add_text_label(&app->circuit, app->pending_text_label_x, app->pending_text_label_y,
                                                 app->canvas_edit_buf);
                if (id >= 0) push_undo(app);
                break;
            }
            case CANVAS_EDIT_TEXT_LABEL: {
                TextLabel *t = &app->circuit.text_labels[app->canvas_edit_id];
                snprintf(t->text, sizeof(t->text), "%s", app->canvas_edit_buf);
                push_undo(app);
                break;
            }
            default: break;
        }
    }
    app->canvas_edit_kind = CANVAS_EDIT_NONE;
    app->canvas_edit_id = -1;
    SDL_StopTextInput();
}

static void cancel_transient_actions(App *app) {
    app->wiring = 0;
    app->wiring_kind = WIRE_KIND_NORMAL;
    clear_wire_node_marks(app);
    app->drag_kind = DRAG_NONE;
    app->drag_attach_count = 0;
    app->panning = 0;
    app->marquee_active = 0;
    app->section_dragging = 0;
    cancel_canvas_edit(app);
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
    /* a modal takes precedence over anything happening underneath it */
    if (app->settings_panel.open) {
        app->settings_panel.open = 0;
        return;
    }
    /* covers a Components-menu IC pick, a lone-copied-IC placement loop, AND
       any other in-progress general paste (app_pending_place_ic only ever
       returns non-NULL for the first two - see its own comment - so
       app->pasting is checked directly too, or Escape would fail to cancel
       a multi-item/non-IC paste ghost). */
    if (app_pending_place_ic(app) != NULL || app->pasting) {
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
    if (app->section_dragging) {
        app->section_dragging = 0;
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

static void set_active_tool(App *app, Tool tool) {
    cancel_transient_actions(app);
    clear_selection(app); /* switching tools means we're moving on, drop any stale selection */
    app->active_tool = tool;
}

/* Each File dropdown action, dispatched to the matching app_* lifecycle
   function (app.h) - Open prompts its dialog before the discard-confirm
   check (no point asking to discard if the user's about to cancel Open
   anyway); Save/Save As/Close Window each already do their own
   confirm/fallback internally. New Window spawns a wholly independent
   process, so it's the only action with no discard check at all. */
static void dispatch_file_menu_item(App *app, FileMenuItem item) {
    char path[ISONIC_PATH_MAX];
    switch (item) {
        case FILE_MENU_NEW_SCHEMATIC:
            if (app_confirm_discard_if_dirty(app)) app_new_schematic(app);
            break;
        case FILE_MENU_NEW_WINDOW:
            platform_spawn_new_instance();
            break;
        case FILE_MENU_OPEN:
            if (platform_open_file_dialog(app->window, path, sizeof(path)) && app_confirm_discard_if_dirty(app)) {
                app_load_from_file(app, path);
            }
            break;
        case FILE_MENU_SAVE:
            app_save_current(app);
            break;
        case FILE_MENU_SAVE_AS:
            app_save_as(app);
            break;
        case FILE_MENU_CLOSE_WINDOW:
            app_close_window(app);
            break;
        default:
            break;
    }
}

/* Returns 1 if the click was fully handled by the taskbar/dropdown(s) (a
   tool or IC was chosen, a File action dispatched, Settings opened, or it
   just hit chrome like a category fold arrow), 0 if it missed everything
   and the caller should treat it as an ordinary canvas click instead (see
   taskbar_handle_click's comment on why a miss can still have closed an
   open dropdown as a side effect). */
static int handle_taskbar_click(App *app, int mx, int my) {
    Tool tool;
    const char *ic_name;
    FileMenuItem file_item;
    TaskbarClickKind kind = taskbar_handle_click(&app->taskbar, mx, my, &tool, &ic_name, &file_item);
    if (kind == TASKBAR_CLICK_TOOL) {
        set_active_tool(app, tool);
        return 1;
    }
    if (kind == TASKBAR_CLICK_IC) {
        set_active_tool(app, TOOL_PLACE_IC);
        app->place_ic_name = ic_name;
        app->place_rotation = 0;
        return 1;
    }
    if (kind == TASKBAR_CLICK_FILE_MENU_ITEM) {
        dispatch_file_menu_item(app, file_item);
        return 1;
    }
    if (kind == TASKBAR_CLICK_SETTINGS) {
        settings_panel_open(&app->settings_panel);
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
    if (result == LAYER_PANEL_CLICK_CHANGED) push_undo(app);
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
    if (new_id >= 0) push_undo(app);
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
        push_undo(app);
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
        push_undo(app);
        return;
    }
    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        if (wire_id == app->selected_wire_id) app->selected_wire_id = -1;
        circuit_remove_wire(&app->circuit, wire_id);
        push_undo(app);
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
/* True if wire_id's given end is already recorded - anchors can legitimately
   coincide (e.g. begin_selection_drag feeds one anchor per endpoint of every
   selected wire, and two selected wires meeting at the same junction point
   contribute the SAME point twice), and without this check a third,
   unselected wire ending at that shared point would get appended to the
   attachment list once per matching anchor instead of once overall -
   apply_drag_attachments would then add that frame's dx/dy to it multiple
   times, moving it 2x (or more, with more coincident anchors/wires) as far
   as everything actually being dragged. */
static int drag_attach_wire_recorded(const App *app, int wire_id, int end) {
    for (int i = 0; i < app->drag_attach_count; i++) {
        if (app->drag_attach_wire_id[i] == wire_id && app->drag_attach_wire_end[i] == end) return 1;
    }
    return 0;
}
static int drag_attach_via_recorded(const App *app, int via_id) {
    for (int i = 0; i < app->drag_attach_via_count; i++) {
        if (app->drag_attach_via_id[i] == via_id) return 1;
    }
    return 0;
}

static void snapshot_drag_attachments(App *app, const GridPoint *anchors, int anchor_count, int exclude_wire_id) {
    app->drag_attach_count = 0;
    app->drag_attach_via_count = 0;
    for (int ai = 0; ai < anchor_count; ai++) {
        int px = anchors[ai].x, py = anchors[ai].y;
        for (int wi = 0; wi < app->circuit.wire_high_water && app->drag_attach_count < MAX_DRAG_ATTACHMENTS; wi++) {
            if (wi == exclude_wire_id) continue;
            Wire *w = &app->circuit.wires[wi];
            if (!w->in_use || w->selected) continue;
            if (w->from_x == px && w->from_y == py && !drag_attach_wire_recorded(app, wi, 0)) {
                app->drag_attach_wire_id[app->drag_attach_count] = wi;
                app->drag_attach_wire_end[app->drag_attach_count] = 0;
                app->drag_attach_count++;
            }
            if (app->drag_attach_count < MAX_DRAG_ATTACHMENTS && w->to_x == px && w->to_y == py &&
                !drag_attach_wire_recorded(app, wi, 1)) {
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
            if (drag_attach_via_recorded(app, vi)) continue;
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

/* Sections/Text Labels have no pins or endpoints anything else could be
   "attached" to, so unlike begin_component_drag/begin_wire_body_drag these
   never need snapshot_drag_attachments - moving one only ever moves itself. */
static void begin_section_body_drag(App *app, int section_id, int gx, int gy) {
    select_section(app, section_id);
    app->drag_kind = DRAG_SECTION_BODY;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
}

static void begin_section_handle_drag(App *app, int section_id, int corner, int gx, int gy) {
    select_section(app, section_id);
    app->drag_kind = DRAG_SECTION_HANDLE;
    app->drag_section_corner = corner;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
}

static void begin_text_label_drag(App *app, int text_label_id, int gx, int gy) {
    select_text_label(app, text_label_id);
    app->drag_kind = DRAG_TEXT_LABEL;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_moved = 0;
}

static int selection_count(const App *app) {
    int n = 0;
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        if (app->circuit.components[i].in_use && app->circuit.components[i].selected) n++;
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        if (app->circuit.wires[i].in_use && app->circuit.wires[i].selected) n++;
    }
    for (int i = 0; i < app->circuit.section_high_water; i++) {
        if (app->circuit.sections[i].in_use && app->circuit.sections[i].selected) n++;
    }
    for (int i = 0; i < app->circuit.text_label_high_water; i++) {
        if (app->circuit.text_labels[i].in_use && app->circuit.text_labels[i].selected) n++;
    }
    return n;
}

/* Recomputes the singular tracked ids (selected_component_id, ...) from
   scratch after a Ctrl+click toggle - same "ambiguous means -1, otherwise
   point at the sole survivor" rule clear_selection/marquee-select already
   established (see clear_selection's own comment above), so anything relying
   on exactly one thing being selected (Ctrl+C copy, Manage Data eligibility,
   double-click rename, ...) keeps working the same regardless of whether
   that single survivor got there via a plain click or a Ctrl+click toggle. */
static void resync_singular_selection_ids(App *app) {
    app->selected_component_id = -1;
    app->selected_wire_id = -1;
    app->selected_section_id = -1;
    app->selected_text_label_id = -1;
    if (selection_count(app) != 1) return;
    for (int i = 0; i < app->circuit.component_high_water; i++) {
        if (app->circuit.components[i].in_use && app->circuit.components[i].selected) {
            app->selected_component_id = i;
            return;
        }
    }
    for (int i = 0; i < app->circuit.wire_high_water; i++) {
        if (app->circuit.wires[i].in_use && app->circuit.wires[i].selected) {
            app->selected_wire_id = i;
            return;
        }
    }
    for (int i = 0; i < app->circuit.section_high_water; i++) {
        if (app->circuit.sections[i].in_use && app->circuit.sections[i].selected) {
            app->selected_section_id = i;
            return;
        }
    }
    for (int i = 0; i < app->circuit.text_label_high_water; i++) {
        if (app->circuit.text_labels[i].in_use && app->circuit.text_labels[i].selected) {
            app->selected_text_label_id = i;
            return;
        }
    }
}

/* Ctrl+click toggle-select: adds/removes exactly one item from whatever else
   is already selected, instead of collapsing the whole selection down to
   just this one item like a plain click does (see select_component and
   friends above) - see handle_left_click's ctrl_held branches. */
static void toggle_component_selected(App *app, int id) {
    app->circuit.components[id].selected = !app->circuit.components[id].selected;
    resync_singular_selection_ids(app);
}
static void toggle_wire_selected(App *app, int id) {
    app->circuit.wires[id].selected = !app->circuit.wires[id].selected;
    resync_singular_selection_ids(app);
}
static void toggle_section_selected(App *app, int id) {
    app->circuit.sections[id].selected = !app->circuit.sections[id].selected;
    resync_singular_selection_ids(app);
}
static void toggle_text_label_selected(App *app, int id) {
    app->circuit.text_labels[id].selected = !app->circuit.text_labels[id].selected;
    resync_singular_selection_ids(app);
}

/* Moving a multi-item selection (marquee or otherwise) drags every already-
   .selected component/wire together, rigidly, by the same per-frame delta -
   unlike begin_component_drag/begin_wire_body_drag, this does NOT touch the
   current selection first, since collapsing it down to just the clicked item
   is exactly the bug this exists to avoid. Anything unselected still
   attached to one of the selection's own anchor points (component pins, or
   a selected wire's own endpoints) is dragged along too, same as a single-
   item drag - see snapshot_drag_attachments. click_component_id/
   click_wire_id/click_section_id/click_text_label_id (all but one -1) is
   whichever single item was actually clicked to start this drag - see the
   drag_click_ and drag_moved fields' comment in app.h and finish_drag: a
   plain click with no movement still collapses down to just that one item.
   Sections/Text Labels contribute no anchors of their own below (see
   begin_section_body_drag's comment) - only components/wires can have
   anything else attached to them. */
static void begin_selection_drag(App *app, int gx, int gy, int click_component_id, int click_wire_id,
                                  int click_section_id, int click_text_label_id) {
    app->drag_kind = DRAG_SELECTION;
    app->drag_last_gx = gx;
    app->drag_last_gy = gy;
    app->drag_click_component_id = click_component_id;
    app->drag_click_wire_id = click_wire_id;
    app->drag_click_section_id = click_section_id;
    app->drag_click_text_label_id = click_text_label_id;
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

/* Recomputes which components/wires/sections/text labels are enclosed by
   the current marquee box and marks them .selected - called on every
   mouse-move during a marquee drag (see app_handle_event) so the selection
   previews live as the box grows/shrinks, instead of only appearing once
   the button is released. Fully enclosed only ("erst markiert, wenn
   vollständig markiert"), not merely touched - a box that only grazes a
   component's edge doesn't grab it. A wire counts as enclosed when both its
   endpoints are inside the box (it has no interior area of its own to
   test); a Text Label the same way its single anchor point does. A LOCKED
   section still gets marquee-selected like anything else (lock only
   protects against move/resize/rename/delete, not selection - see
   circuit.h). Recomputed from scratch every call (clearing first) rather
   than incrementally, so shrinking the box correctly drops things it no
   longer covers. */
static void update_marquee_selection(App *app) {
    Circuit *circuit = &app->circuit;
    for (int i = 0; i < circuit->component_high_water; i++) circuit->components[i].selected = 0;
    for (int i = 0; i < circuit->wire_high_water; i++) circuit->wires[i].selected = 0;
    for (int i = 0; i < circuit->section_high_water; i++) circuit->sections[i].selected = 0;
    for (int i = 0; i < circuit->text_label_high_water; i++) circuit->text_labels[i].selected = 0;

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
    for (int i = 0; i < circuit->section_high_water; i++) {
        Section *s = &circuit->sections[i];
        /* a LOCKED section can't be highlighted/selected at all, not even
           by marquee - see circuit.h's Section comment and the matching
           exclusion in handle_left_click's own section-label/body checks. */
        if (!s->in_use || s->locked) continue;
        if (s->x0 >= min_x && s->x1 <= max_x && s->y0 >= min_y && s->y1 <= max_y) {
            s->selected = 1;
        }
    }
    for (int i = 0; i < circuit->text_label_high_water; i++) {
        TextLabel *t = &circuit->text_labels[i];
        if (!t->in_use) continue;
        if (t->x >= min_x && t->x <= max_x && t->y >= min_y && t->y <= max_y) {
            t->selected = 1;
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

/* TOOL_SECTION mouse-up: turns the just-dragged box into a grid rect and
   hands it to canvas_edit_kind for its first-time label entry, rather than
   adding it to the circuit directly - see CANVAS_EDIT_NEW_SECTION and
   commit_canvas_edit. The start corner (section_drag_start_gx/gy) was
   already pinned to a grid point back when the drag began - see its own
   comment in app.h; only the release point (mx, my) still needs converting
   here, fresh, same "one last catch-up conversion in case the final
   position never got a motion event of its own" reasoning
   finish_marquee_select's own re-run of update_marquee_selection relies on.
   Too small a drag (below SECTION_MIN_SIZE either axis - including a plain
   click with no real drag at all) discards it instead of creating a
   degenerate sliver nobody could see or select afterward. */
static void finish_section_draw(App *app, int mx, int my) {
    app->section_dragging = 0;
    int gx1, gy1;
    camera_screen_to_grid(&app->camera, mx, my, &gx1, &gy1);
    int gx0 = app->section_drag_start_gx, gy0 = app->section_drag_start_gy;
    int lo_x = gx0 < gx1 ? gx0 : gx1, hi_x = gx0 > gx1 ? gx0 : gx1;
    int lo_y = gy0 < gy1 ? gy0 : gy1, hi_y = gy0 > gy1 ? gy0 : gy1;
    if (hi_x - lo_x < SECTION_MIN_SIZE || hi_y - lo_y < SECTION_MIN_SIZE) return;

    app->canvas_edit_kind = CANVAS_EDIT_NEW_SECTION;
    app->canvas_edit_id = -1;
    app->pending_section_x0 = lo_x;
    app->pending_section_y0 = lo_y;
    app->pending_section_x1 = hi_x;
    app->pending_section_y1 = hi_y;
    app->canvas_edit_buf[0] = '\0';
    app->canvas_edit_len = 0;
    SDL_StartTextInput();
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

/* Section corner-resize handles: a small fixed screen-pixel radius around
   each handle's exact position (section_corner_screen_pos), same idea as
   TERMINAL_HIT_PADDING_PX above but generous enough for a genuinely tiny
   target. Only a currently-.selected, unlocked section's handles are
   offered at all - render_sections only draws them under that same
   condition (locked ones can't be resized; an unselected one hasn't had
   its handles revealed yet), so this stays consistent with what's actually
   on screen. */
#define SECTION_HANDLE_HIT_PX 8

static int find_section_handle_at(App *app, int mx, int my, int *out_section_id, int *out_corner) {
    Circuit *circuit = &app->circuit;
    for (int i = circuit->section_high_water - 1; i >= 0; i--) {
        Section *s = &circuit->sections[i];
        if (!s->in_use || !s->selected || s->locked) continue;
        for (int corner = 0; corner < 4; corner++) {
            int hx, hy;
            section_corner_screen_pos(&app->camera, s, corner, &hx, &hy);
            if (mx >= hx - SECTION_HANDLE_HIT_PX && mx <= hx + SECTION_HANDLE_HIT_PX &&
                my >= hy - SECTION_HANDLE_HIT_PX && my <= hy + SECTION_HANDLE_HIT_PX) {
                *out_section_id = i;
                *out_corner = corner;
                return 1;
            }
        }
    }
    return 0;
}

/* A section's label text and lock icon - screen-space widgets computed the
   same way render_sections itself draws them (section_label_bounds), so a
   click always lands exactly where it visually looks like it should. Any
   section offers these, not just a selected one - unlike the resize
   handles, selecting/locking/renaming doesn't require having selected it
   first. */
static int find_section_label_or_lock_at(App *app, int mx, int my, int *out_section_id, int *out_is_lock) {
    Circuit *circuit = &app->circuit;
    for (int i = circuit->section_high_water - 1; i >= 0; i--) {
        Section *s = &circuit->sections[i];
        if (!s->in_use) continue;
        SDL_Rect label_rect, lock_rect;
        if (!section_label_bounds(app->font_large, &app->camera, s, &label_rect, &lock_rect)) continue;
        /* the lock icon is only a valid click target while it's actually
           being drawn (see render_sections/section_lock_icon_visible) - mx,my
           doubles as "the cursor's current hover position" here, exactly
           matching what this same frame rendered it with. */
        if (section_lock_icon_visible(app->font_large, &app->camera, s, mx, my) &&
            mx >= lock_rect.x && mx < lock_rect.x + lock_rect.w && my >= lock_rect.y && my < lock_rect.y + lock_rect.h) {
            *out_section_id = i;
            *out_is_lock = 1;
            return 1;
        }
        if (mx >= label_rect.x && mx < label_rect.x + label_rect.w && my >= label_rect.y && my < label_rect.y + label_rect.h) {
            *out_section_id = i;
            *out_is_lock = 0;
            return 1;
        }
    }
    return 0;
}

static int find_text_label_at(App *app, int mx, int my) {
    Circuit *circuit = &app->circuit;
    for (int i = circuit->text_label_high_water - 1; i >= 0; i--) {
        TextLabel *t = &circuit->text_labels[i];
        if (!t->in_use) continue;
        SDL_Rect bounds;
        if (!text_label_bounds(app->font_large, &app->camera, t, &bounds)) continue;
        if (mx >= bounds.x && mx < bounds.x + bounds.w && my >= bounds.y && my < bounds.y + bounds.h) return i;
    }
    return -1;
}

/* True if layer_slot still refers to a real, in-use layer - a copied wire/
   via's stored layer_slot(s) are pasted back as-is (see ClipboardWire/
   ClipboardVia in app.h), but the source layer could in principle have been
   removed between copy and paste; this is what commit_paste falls back on
   in that rare case. */
static int layer_slot_valid(const Circuit *circuit, int layer_slot) {
    return layer_slot >= 0 && layer_slot < MAX_LAYERS && circuit->layers[layer_slot].in_use;
}

/* Grows a running (min_x, min_y) bounding corner to also cover (px, py) -
   *have_bounds starts false and is set true on the first call, so the very
   first point always "wins" instead of being compared against an arbitrary
   sentinel. Shared by copy_selection's bounding-box pass below across every
   clipboard item type. */
static void grow_bounds(int px, int py, int *min_x, int *min_y, int *have_bounds) {
    if (!*have_bounds || px < *min_x) *min_x = px;
    if (!*have_bounds || py < *min_y) *min_y = py;
    *have_bounds = 1;
}

/* Ctrl+C - snapshots the current selection (any mix of components/wires/
   sections/text labels, but never a via - see circuit.h's Via, which has no
   .selected field and so can't be part of a selection to begin with) into
   the clipboard_* arrays, each item stored as a (dx,dy) offset from the
   copied selection's own bounding-box min corner - see the ClipboardXxx
   structs in app.h. If nothing is currently selected, falls back to
   whatever single thing is directly under the cursor (a Component, a Wire,
   a Section's outline/corners - same border-only hit-test click uses, see
   circuit_find_section_at, and excluding a locked one same as every other
   hit-test in Select mode does - or a Text Label, checked in that priority
   order) - the same "hover and copy, no need to click first" convenience
   the old single-item copy always had, now also covering wires. Arms
   `pasting` afterward so the ghost/click-to-place immediately starts, same
   as it always has - see paste_clipboard/commit_paste and
   clipboard_is_single_ic for what happens depending on what got copied. */
static void copy_selection(App *app) {
    int have_selection = selection_count(app) > 0;
    int hover_comp = -1, hover_wire = -1, hover_section = -1, hover_text = -1;

    if (!have_selection) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (my >= TASKBAR_HEIGHT) {
            int box_gx, box_gy;
            camera_screen_to_grid_floor(&app->camera, mx, my, &box_gx, &box_gy);
            hover_comp = circuit_find_component_at(&app->circuit, box_gx, box_gy);
            if (hover_comp < 0) {
                float fx, fy;
                camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);
                hover_wire = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
                if (hover_wire < 0) {
                    hover_section = circuit_find_section_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
                    if (hover_section >= 0 && app->circuit.sections[hover_section].locked) hover_section = -1;
                    if (hover_section < 0) hover_text = find_text_label_at(app, mx, my);
                }
            }
        }
        if (hover_comp < 0 && hover_wire < 0 && hover_section < 0 && hover_text < 0) return;
    }

    /* gather every matching item, absolute grid coordinates for now - offsets
       are rebased below, once the whole set's own bounding box is known */
    app->clipboard_component_count = 0;
    app->clipboard_wire_count = 0;
    app->clipboard_via_count = 0;
    app->clipboard_section_count = 0;
    app->clipboard_text_label_count = 0;

    for (int i = 0; i < app->circuit.component_high_water && app->clipboard_component_count < CLIPBOARD_MAX_COMPONENTS; i++) {
        Component *c = &app->circuit.components[i];
        if (!c->in_use || (have_selection ? !c->selected : i != hover_comp)) continue;
        ClipboardComponent *cc = &app->clipboard_components[app->clipboard_component_count++];
        cc->ic_def = c->ic_def;
        cc->dx = c->grid_x;
        cc->dy = c->grid_y;
        cc->rotation = c->rotation;
    }
    for (int i = 0; i < app->circuit.wire_high_water && app->clipboard_wire_count < CLIPBOARD_MAX_WIRES; i++) {
        Wire *w = &app->circuit.wires[i];
        if (!w->in_use || (have_selection ? !w->selected : i != hover_wire)) continue;
        ClipboardWire *cw = &app->clipboard_wires[app->clipboard_wire_count++];
        cw->from_dx = w->from_x;
        cw->from_dy = w->from_y;
        cw->to_dx = w->to_x;
        cw->to_dy = w->to_y;
        cw->kind = w->kind;
        cw->input_value = w->input_value;
        cw->layer_slot = w->layer_slot;
    }
    /* a via is never independently selected/hovered (see ClipboardVia's own
       comment in app.h) - it's only ever pulled in as a side effect of a
       wire that WAS just gathered above ending exactly on it, checked
       against the wire coords just stored (still absolute at this point,
       same as the via's own x,y) rather than re-scanning the circuit's
       selection state a second time. Each via is considered at most once
       even if several copied wires converge on it (e.g. a 3+-way junction). */
    for (int vi = 0; vi < app->circuit.via_high_water && app->clipboard_via_count < CLIPBOARD_MAX_VIAS; vi++) {
        Via *v = &app->circuit.vias[vi];
        if (!v->in_use) continue;
        int attached = 0;
        for (int wi = 0; wi < app->clipboard_wire_count; wi++) {
            const ClipboardWire *cw = &app->clipboard_wires[wi];
            if ((cw->from_dx == v->x && cw->from_dy == v->y) || (cw->to_dx == v->x && cw->to_dy == v->y)) {
                attached = 1;
                break;
            }
        }
        if (!attached) continue;
        ClipboardVia *cv = &app->clipboard_vias[app->clipboard_via_count++];
        cv->dx = v->x;
        cv->dy = v->y;
        cv->layer_slot_a = v->layer_slot_a;
        cv->layer_slot_b = v->layer_slot_b;
    }
    for (int i = 0; i < app->circuit.section_high_water && app->clipboard_section_count < CLIPBOARD_MAX_SECTIONS; i++) {
        Section *s = &app->circuit.sections[i];
        if (!s->in_use || (have_selection ? !s->selected : i != hover_section)) continue;
        ClipboardSection *cs = &app->clipboard_sections[app->clipboard_section_count++];
        cs->dx0 = s->x0;
        cs->dy0 = s->y0;
        cs->dx1 = s->x1;
        cs->dy1 = s->y1;
        /* a locked source's lock state is deliberately NOT copied - a
           pasted copy always starts unlocked, since locking is a deliberate
           protective action the user can reapply if they want it again */
        snprintf(cs->label, sizeof(cs->label), "%s", s->label);
    }
    for (int i = 0; i < app->circuit.text_label_high_water && app->clipboard_text_label_count < CLIPBOARD_MAX_TEXT_LABELS; i++) {
        TextLabel *t = &app->circuit.text_labels[i];
        if (!t->in_use || (have_selection ? !t->selected : i != hover_text)) continue;
        ClipboardTextLabel *ct = &app->clipboard_text_labels[app->clipboard_text_label_count++];
        ct->dx = t->x;
        ct->dy = t->y;
        snprintf(ct->text, sizeof(ct->text), "%s", t->text);
    }

    if (clipboard_is_empty(app)) return;

    /* rebase every just-gathered absolute coordinate to an offset from the
       whole set's own bounding-box min corner */
    int min_x = 0, min_y = 0, have_bounds = 0;
    for (int i = 0; i < app->clipboard_component_count; i++) {
        grow_bounds(app->clipboard_components[i].dx, app->clipboard_components[i].dy, &min_x, &min_y, &have_bounds);
    }
    for (int i = 0; i < app->clipboard_wire_count; i++) {
        grow_bounds(app->clipboard_wires[i].from_dx, app->clipboard_wires[i].from_dy, &min_x, &min_y, &have_bounds);
        grow_bounds(app->clipboard_wires[i].to_dx, app->clipboard_wires[i].to_dy, &min_x, &min_y, &have_bounds);
    }
    /* redundant with the wire pass above (a copied via always sits exactly
       on one of a copied wire's endpoints, see the gather pass) but cheap
       and keeps this pass reading as "every clipboard array", not relying
       on that invariant to skip one */
    for (int i = 0; i < app->clipboard_via_count; i++) {
        grow_bounds(app->clipboard_vias[i].dx, app->clipboard_vias[i].dy, &min_x, &min_y, &have_bounds);
    }
    for (int i = 0; i < app->clipboard_section_count; i++) {
        grow_bounds(app->clipboard_sections[i].dx0, app->clipboard_sections[i].dy0, &min_x, &min_y, &have_bounds);
    }
    for (int i = 0; i < app->clipboard_text_label_count; i++) {
        grow_bounds(app->clipboard_text_labels[i].dx, app->clipboard_text_labels[i].dy, &min_x, &min_y, &have_bounds);
    }

    for (int i = 0; i < app->clipboard_component_count; i++) {
        app->clipboard_components[i].dx -= min_x;
        app->clipboard_components[i].dy -= min_y;
    }
    for (int i = 0; i < app->clipboard_wire_count; i++) {
        app->clipboard_wires[i].from_dx -= min_x;
        app->clipboard_wires[i].from_dy -= min_y;
        app->clipboard_wires[i].to_dx -= min_x;
        app->clipboard_wires[i].to_dy -= min_y;
    }
    for (int i = 0; i < app->clipboard_via_count; i++) {
        app->clipboard_vias[i].dx -= min_x;
        app->clipboard_vias[i].dy -= min_y;
    }
    for (int i = 0; i < app->clipboard_section_count; i++) {
        app->clipboard_sections[i].dx0 -= min_x;
        app->clipboard_sections[i].dy0 -= min_y;
        app->clipboard_sections[i].dx1 -= min_x;
        app->clipboard_sections[i].dy1 -= min_y;
    }
    for (int i = 0; i < app->clipboard_text_label_count; i++) {
        app->clipboard_text_labels[i].dx -= min_x;
        app->clipboard_text_labels[i].dy -= min_y;
    }

    cancel_transient_actions(app);
    app->pasting = 1;
    app->place_rotation = 0;
}

/* Ctrl+V - just re-arms `pasting` (a no-op if nothing's been copied this
   session, see clipboard_is_empty), same as Ctrl+C already does at the end
   of copy_selection above; lets you place another copy after the first's
   already been dropped, without re-copying. Committing the copied content
   into the circuit happens on the next click instead - see
   handle_left_click's place_def/commit_paste branches below, and
   clipboard_is_single_ic for why a lone copied IC behaves differently
   (infinite click-to-place loop) from everything else (one-shot paste). */
static void paste_clipboard(App *app) {
    if (clipboard_is_empty(app)) return;
    cancel_transient_actions(app);
    app->pasting = 1;
    app->place_rotation = 0;
}

/* Commits every item in the clipboard into the circuit at once, anchored at
   (gx,gy) - each item's stored (dx,dy) offset (see copy_selection) is just
   added straight to it. Ends `pasting` immediately (one-shot, unlike a lone
   copied IC's infinite click-to-place loop, see handle_left_click) and
   leaves every freshly-placed item selected, replacing whatever was
   selected before, so the user can immediately nudge the whole pasted group
   if it landed slightly off. Never called for a lone copied IC - see
   clipboard_is_single_ic. */
static void commit_paste(App *app, int gx, int gy) {
    clear_selection(app);
    for (int i = 0; i < app->clipboard_component_count; i++) {
        const ClipboardComponent *cc = &app->clipboard_components[i];
        int id = circuit_add_ic(&app->circuit, gx + cc->dx, gy + cc->dy, cc->ic_def);
        if (id >= 0) {
            app->circuit.components[id].rotation = cc->rotation;
            app->circuit.components[id].selected = 1;
        }
    }
    for (int i = 0; i < app->clipboard_wire_count; i++) {
        const ClipboardWire *cw = &app->clipboard_wires[i];
        /* preserves the original's layer rather than always using whatever's
           active right now - falls back to the active layer only if that
           original layer is somehow gone by paste time (see
           layer_slot_valid) */
        int layer_slot = layer_slot_valid(&app->circuit, cw->layer_slot) ? cw->layer_slot : app->active_layer_slot;
        int id = circuit_add_wire(&app->circuit, gx + cw->from_dx, gy + cw->from_dy, gx + cw->to_dx, gy + cw->to_dy,
                                   cw->kind, layer_slot);
        if (id >= 0) {
            app->circuit.wires[id].input_value = cw->input_value;
            app->circuit.wires[id].selected = 1;
        }
    }
    /* a via with either side's layer gone by paste time is just dropped -
       there's no sensible "active layer" fallback for a via the way a wire
       has one (which OTHER layer would it even bridge to?), and this is
       already the rare edge case layer_slot_valid exists for */
    for (int i = 0; i < app->clipboard_via_count; i++) {
        const ClipboardVia *cv = &app->clipboard_vias[i];
        if (!layer_slot_valid(&app->circuit, cv->layer_slot_a) || !layer_slot_valid(&app->circuit, cv->layer_slot_b)) continue;
        circuit_add_via(&app->circuit, gx + cv->dx, gy + cv->dy, cv->layer_slot_a, cv->layer_slot_b);
    }
    for (int i = 0; i < app->clipboard_section_count; i++) {
        const ClipboardSection *cs = &app->clipboard_sections[i];
        int id = circuit_add_section(&app->circuit, gx + cs->dx0, gy + cs->dy0, gx + cs->dx1, gy + cs->dy1, cs->label);
        if (id >= 0) app->circuit.sections[id].selected = 1;
    }
    for (int i = 0; i < app->clipboard_text_label_count; i++) {
        const ClipboardTextLabel *ct = &app->clipboard_text_labels[i];
        int id = circuit_add_text_label(&app->circuit, gx + ct->dx, gy + ct->dy, ct->text);
        if (id >= 0) app->circuit.text_labels[id].selected = 1;
    }
    app->pasting = 0;
    /* -1 (ambiguous) unless exactly one thing above actually landed - same
       "point at the sole survivor, else -1" rule Ctrl+click toggling
       already established, see its own comment */
    resync_singular_selection_ids(app);
    push_undo(app);
}

/* double_click gates starting a Section-label/Text-Label rename - a plain
   click just selects, same "single click selects, double click renames"
   split layer_panel.c's own name field uses. */
static void handle_left_click(App *app, int mx, int my, int gx, int gy, float fx, float fy, int double_click,
                               int ctrl_held) {
    /* clicking an Input's H/L label always toggles it, no matter what tool is
       active or what else that click would otherwise do */
    int input_wire_id = find_input_terminal_at(app, mx, my);
    if (input_wire_id >= 0) {
        app->circuit.wires[input_wire_id].input_value = !app->circuit.wires[input_wire_id].input_value;
        return; /* toggling is not selecting - leave whatever was selected before untouched */
    }

    /* covers both an IC chosen from the Components menu (TOOL_PLACE_IC) and
       an in-progress Ctrl+C/Ctrl+V paste of a single copied IC and nothing
       else - app_pending_place_ic resolves which one wins (see its comment
       in app.h). Either way, stays in the current mode after placing (like
       Wire/Input/Output do) so several copies can be dropped in a row
       without reselecting the IC each time - unchanged from before the
       general multi-item clipboard below existed. */
    const IC_Def *place_def = app_pending_place_ic(app);
    if (place_def != NULL) {
        int w, h;
        ic_dip_body_size(place_def->pin_count, &w, &h);
        if (app->place_rotation & 1) { int t = w; w = h; h = t; }
        if (!circuit_footprint_overlaps(&app->circuit, gx, gy, w, h, -1)) {
            int new_id = circuit_add_ic(&app->circuit, gx, gy, place_def);
            if (new_id >= 0) {
                app->circuit.components[new_id].rotation = app->place_rotation;
                select_component(app, new_id);
                push_undo(app);
            }
        }
        return;
    }

    /* any other in-progress paste (a lone copied wire/Section/Text Label, or
       more than one item of any kind/mix) commits everything at once right
       here and ends `pasting` immediately - a one-shot placement, unlike the
       lone-IC loop just above (see clipboard_is_single_ic/commit_paste). */
    if (app->pasting) {
        commit_paste(app, gx, gy);
        return;
    }

    if (app->active_tool == TOOL_WIRE || app->active_tool == TOOL_INPUT || app->active_tool == TOOL_OUTPUT) {
        begin_wire_from(app, gx, gy, tool_to_wire_kind(app->active_tool));
        return;
    }

    /* Section-Labeling: drag out a rectangle - see finish_section_draw and
       app.h's section_dragging comment on why both corners are tracked in
       grid coordinates, not screen pixels. No hit-test to fall through
       first, unlike TOOL_SELECT below - this tool only ever means "start
       drawing a new section", nothing else. */
    if (app->active_tool == TOOL_SECTION) {
        clear_selection(app);
        app->section_dragging = 1;
        app->section_drag_start_gx = app->section_drag_cur_gx = gx;
        app->section_drag_start_gy = app->section_drag_cur_gy = gy;
        return;
    }

    /* Text Label: a single click places one and immediately starts typing
       it - see CANVAS_EDIT_NEW_TEXT_LABEL/commit_canvas_edit. */
    if (app->active_tool == TOOL_TEXT_LABEL) {
        clear_selection(app);
        app->canvas_edit_kind = CANVAS_EDIT_NEW_TEXT_LABEL;
        app->canvas_edit_id = -1;
        app->pending_text_label_x = gx;
        app->pending_text_label_y = gy;
        app->canvas_edit_buf[0] = '\0';
        app->canvas_edit_len = 0;
        SDL_StartTextInput();
        return;
    }

    /* TOOL_SELECT - wires are only ever started from the Wire/Input/Output tool
       now; Select mode does not let you drag a new wire off a pin, even by
       clicking one. It does let you drag a wire's node (every endpoint
       coincident at that point moves together), or the body of a wire/IC
       (moving it as a whole, dragging along anything attached at its
       endpoints/pins).

       A section's corner handles and its label/lock icon are checked first,
       above even node-picking - small, precise, deliberately-placed targets
       that should always win over whatever circuit content happens to sit
       under/behind them (a section itself deliberately renders as a
       background layer, see render_sections). Its plain rectangle BODY, by
       contrast, is checked LAST, after every other kind of hit-test below -
       it's a big diffuse area that should always lose to anything more
       specific drawn on top of it. */
    int handle_section_id, handle_corner;
    if (find_section_handle_at(app, mx, my, &handle_section_id, &handle_corner)) {
        begin_section_handle_drag(app, handle_section_id, handle_corner, gx, gy);
        return;
    }

    int label_section_id, label_is_lock;
    if (find_section_label_or_lock_at(app, mx, my, &label_section_id, &label_is_lock)) {
        Section *s = &app->circuit.sections[label_section_id];
        if (label_is_lock) {
            /* the lock icon itself always stays clickable regardless of
               current state - otherwise a locked section could never be
               unlocked again. Locking one that was selected also drops the
               selection - it can't be highlighted while locked (see below),
               so it shouldn't stay looking selected either. */
            s->locked = !s->locked;
            if (s->locked && s->selected) {
                s->selected = 0;
                if (app->selected_section_id == label_section_id) app->selected_section_id = -1;
            }
            push_undo(app);
        } else if (!s->locked) {
            /* a LOCKED section's label deliberately does nothing here - not
               selectable, not renameable, see circuit.h's Section comment
               and update_marquee_selection's identical exclusion. */
            if (double_click) {
                select_section(app, label_section_id);
                app->canvas_edit_kind = CANVAS_EDIT_SECTION_LABEL;
                app->canvas_edit_id = label_section_id;
                snprintf(app->canvas_edit_buf, sizeof(app->canvas_edit_buf), "%s", s->label);
                app->canvas_edit_len = (int)strlen(app->canvas_edit_buf);
                SDL_StartTextInput();
            } else if (ctrl_held) {
                toggle_section_selected(app, label_section_id);
            } else {
                select_section(app, label_section_id);
            }
        }
        return;
    }

    int text_label_id = find_text_label_at(app, mx, my);
    if (text_label_id >= 0) {
        TextLabel *t = &app->circuit.text_labels[text_label_id];
        if (double_click) {
            select_text_label(app, text_label_id);
            app->canvas_edit_kind = CANVAS_EDIT_TEXT_LABEL;
            app->canvas_edit_id = text_label_id;
            snprintf(app->canvas_edit_buf, sizeof(app->canvas_edit_buf), "%s", t->text);
            app->canvas_edit_len = (int)strlen(app->canvas_edit_buf);
            SDL_StartTextInput();
        } else if (ctrl_held) {
            toggle_text_label_selected(app, text_label_id);
        } else if (t->selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, -1, -1, -1, text_label_id);
        } else {
            begin_text_label_drag(app, text_label_id, gx, gy);
        }
        return;
    }

    /* Node-picking goes first among the "ordinary circuit content" checks
       since it's the most precise target and must win over a component/wire
       body sitting under it. */
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
        if (ctrl_held) {
            toggle_component_selected(app, comp_id);
        } else if (app->circuit.components[comp_id].selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, comp_id, -1, -1, -1);
        } else {
            begin_component_drag(app, comp_id, gx, gy);
        }
        return;
    }

    int wire_id = circuit_find_wire_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (wire_id >= 0) {
        if (ctrl_held) {
            toggle_wire_selected(app, wire_id);
        } else if (app->circuit.wires[wire_id].selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, -1, wire_id, -1, -1);
        } else {
            begin_wire_body_drag(app, wire_id, gx, gy);
        }
        return;
    }

    /* on the section's own OUTLINE (or a corner) only - box_gx/box_gy would
       hit-test its filled interior instead, which is deliberately NOT a
       click target (see circuit_find_section_at) so components/wires drawn
       inside a section, and empty space around them, both stay fully
       click-through. */
    int box_section_id = circuit_find_section_at(&app->circuit, fx, fy, app_wire_hit_tolerance(app));
    if (box_section_id >= 0 && !app->circuit.sections[box_section_id].locked) {
        Section *s = &app->circuit.sections[box_section_id];
        if (ctrl_held) {
            toggle_section_selected(app, box_section_id);
        } else if (s->selected && selection_count(app) > 1) {
            begin_selection_drag(app, gx, gy, -1, -1, box_section_id, -1);
        } else {
            begin_section_body_drag(app, box_section_id, gx, gy);
        }
        return;
    }
    /* a LOCKED section is fully non-interactive here (not even selectable -
       see circuit.h's Section comment) - falls through to whatever's below
       instead of returning, same as if nothing were hit at all. */

    /* Ctrl+click on truly empty space leaves the current selection alone
       instead of starting a rubber-band box - a marquee always clears first
       (see begin_marquee_select), which would fight the "build up a
       selection one Ctrl+click at a time" purpose Ctrl held here signals. */
    if (ctrl_held) return;

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
    if (new_id >= 0) push_undo(app); /* -1 means from == to - no wire was actually added */
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
        else if (app->drag_click_section_id >= 0) select_section(app, app->drag_click_section_id);
        else if (app->drag_click_text_label_id >= 0) select_text_label(app, app->drag_click_text_label_id);
    }
    if (app->drag_moved) push_undo(app);
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
            if (settings_panel_covers_point(&app->settings_panel, mx, my)) break;
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
               to the canvas" fallback meaning for these buttons anyway.
               Settings is a true modal though, so it still wins over even
               these - undoing/redoing the canvas underneath an open popup
               would be surprising. */
            if (event->button.button == SDL_BUTTON_X1) {
                if (!app->settings_panel.open) perform_undo(app);
                break;
            }
            if (event->button.button == SDL_BUTTON_X2) {
                if (!app->settings_panel.open) perform_redo(app);
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
                /* A Section/Text-Label edit in progress owns the keyboard
                   entirely (see the SDL_KEYDOWN case) but has no bounded
                   "field rect" of its own the way layer_panel.c's rename
                   field does - any left click anywhere else in the app
                   (including one that's about to open Settings, or start an
                   entirely different action) commits it first (or silently
                   discards it if nothing was typed), same "click away
                   confirms" rule, just app-wide instead of panel-local. */
                commit_canvas_edit(app);

                /* Settings is a true modal - while open it must intercept
                   every click before anything else gets a chance, including
                   clicks on the File/Settings buttons themselves (clicking
                   File while Settings is open should just close Settings,
                   not also open the File dropdown that same click). */
                if (settings_panel_handle_click(&app->settings_panel, mx, my) != SETTINGS_PANEL_CLICK_MISS) break;
                if (handle_taskbar_click(app, mx, my)) break;
                if (data_editor_handle_click(&app->data_editor, data_editor_eligible(&app->circuit), mx, my)) break;
                if (handle_layer_panel_click(app, mx, my, event->button.clicks >= 2)) break;
            } else if (taskbar_covers_point(&app->taskbar, mx, my) ||
                       data_editor_covers_point(&app->data_editor, mx, my) ||
                       layer_panel_covers_point(&app->layer_panel, mx, my) ||
                       settings_panel_covers_point(&app->settings_panel, mx, my)) {
                break;
            }
            if (my < TASKBAR_HEIGHT) break; /* an empty gap in the taskbar strip itself */
            int gx, gy;
            float fx, fy;
            camera_screen_to_grid(&app->camera, mx, my, &gx, &gy);
            camera_screen_to_grid_f(&app->camera, mx, my, &fx, &fy);
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (app->active_tool == TOOL_VIA) handle_via_tool_click(app, fx, fy);
                else handle_left_click(app, mx, my, gx, gy, fx, fy, event->button.clicks >= 2,
                                        (SDL_GetModState() & KMOD_CTRL) != 0);
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                /* double middle-click resets the camera to its default
                   position/zoom (camera_init's own values - the same
                   "origin" a brand new schematic starts at) - a quick way
                   back after panning/zooming off into empty space, without
                   hunting for the circuit by scrolling around. Still starts
                   a pan below either way, same as a single click always
                   has - if the second click's press-drag continues into a
                   real pan, it now just does so from the freshly-reset
                   view. */
                if (event->button.clicks >= 2) camera_init(&app->camera);
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
                else if (app->section_dragging) finish_section_draw(app, mx, my);
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
            if (app->section_dragging) {
                camera_screen_to_grid(&app->camera, event->motion.x, event->motion.y,
                                       &app->section_drag_cur_gx, &app->section_drag_cur_gy);
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
                    } else if (app->drag_kind == DRAG_SECTION_BODY) {
                        Section *s = &app->circuit.sections[app->selected_section_id];
                        s->x0 += dx; s->x1 += dx;
                        s->y0 += dy; s->y1 += dy;
                    } else if (app->drag_kind == DRAG_SECTION_HANDLE) {
                        /* only the one grabbed corner moves; clamped against
                           its opposite (fixed) corner so it can never cross
                           past it and collapse/invert below
                           SECTION_MIN_SIZE - see circuit.h. Mutated directly
                           here rather than through circuit_set_section_rect,
                           same as every other drag kind above updates its
                           own fields directly instead of going through a
                           circuit.c setter. */
                        Section *s = &app->circuit.sections[app->selected_section_id];
                        int corner = app->drag_section_corner;
                        if (corner == 0 || corner == 2) s->x0 += dx; else s->x1 += dx;
                        if (corner == 0 || corner == 1) s->y0 += dy; else s->y1 += dy;
                        if (s->x1 - s->x0 < SECTION_MIN_SIZE) {
                            if (corner == 0 || corner == 2) s->x0 = s->x1 - SECTION_MIN_SIZE;
                            else s->x1 = s->x0 + SECTION_MIN_SIZE;
                        }
                        if (s->y1 - s->y0 < SECTION_MIN_SIZE) {
                            if (corner == 0 || corner == 1) s->y0 = s->y1 - SECTION_MIN_SIZE;
                            else s->y1 = s->y0 + SECTION_MIN_SIZE;
                        }
                    } else if (app->drag_kind == DRAG_TEXT_LABEL) {
                        TextLabel *t = &app->circuit.text_labels[app->selected_text_label_id];
                        t->x += dx;
                        t->y += dy;
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
                        /* a LOCKED section stays put even while part of a
                           bigger selection being dragged - see circuit.h's
                           Section comment and delete_selection's identical
                           reasoning. */
                        for (int i = 0; i < app->circuit.section_high_water; i++) {
                            Section *s = &app->circuit.sections[i];
                            if (!s->in_use || !s->selected || s->locked) continue;
                            s->x0 += dx; s->x1 += dx;
                            s->y0 += dy; s->y1 += dy;
                        }
                        for (int i = 0; i < app->circuit.text_label_high_water; i++) {
                            TextLabel *t = &app->circuit.text_labels[i];
                            if (!t->in_use || !t->selected) continue;
                            t->x += dx;
                            t->y += dy;
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

            /* a keybind capture or the Settings autosave field owns every
               key while active - it must be checked before anything else
               below, including layer_panel's own editing gate, the same way
               a modal takes priority over the canvas in handle_escape. */
            if (settings_panel_is_capturing_key(&app->settings_panel)) {
                settings_panel_handle_key(&app->settings_panel, sc);
                break;
            }
            /* the popup is still a modal even when idle (not currently
               capturing a keybind or editing the autosave field) - every
               canvas shortcut below must stay swallowed while it's open, or
               e.g. Ctrl+Z would undo the circuit underneath it. Escape still
               closes it (see handle_escape's own top-priority check). */
            if (app->settings_panel.open) {
                if (sc == SDL_SCANCODE_ESCAPE) handle_escape(app);
                break;
            }

            /* while renaming/adding a layer, the text-input widget owns
               every key - typing "w"/"v"/a digit must not also switch tools
               or the active layer out from under the field being edited */
            if (layer_panel_is_editing(&app->layer_panel)) {
                LayerPanelClickResult result = layer_panel_handle_key(&app->layer_panel, &app->circuit, sc);
                if (result == LAYER_PANEL_CLICK_CHANGED) push_undo(app);
                break;
            }

            /* Same idea for a Section/Text-Label edit in progress - owns
               every key while active (Backspace must erase a character, not
               delete_selection() the section itself out from under the
               field being typed). Escape here CANCELS outright (never
               commits), unlike a click elsewhere or Enter - same split
               layer_panel_handle_key's own Escape/Enter make. */
            if (app->canvas_edit_kind != CANVAS_EDIT_NONE) {
                if (sc == SDL_SCANCODE_BACKSPACE) {
                    if (app->canvas_edit_len > 0) {
                        app->canvas_edit_len--;
                        app->canvas_edit_buf[app->canvas_edit_len] = '\0';
                    }
                } else if (sc == SDL_SCANCODE_ESCAPE) {
                    cancel_canvas_edit(app);
                } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                    commit_canvas_edit(app);
                }
                break;
            }

            /* every plain (unmodified) tool-switch key below explicitly
               excludes Ctrl - without this, Ctrl+<key> would BOTH switch
               tools AND trigger whatever the Ctrl-modified shortcut below it
               is (this is exactly how Via's old default of V collided with
               Ctrl+V once Paste got its own keybind: pressing Ctrl+V to
               paste also silently switched to the Via tool as a side
               effect). Copy/Paste/Undo/Redo already require Ctrl themselves
               (checked further below) so this only ever excludes a
               plain-key tool switch from firing, never blocks the
               Ctrl-modified action it might collide with. */
            int no_ctrl = !(event->key.keysym.mod & KMOD_CTRL);

            if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
                delete_selection(app);
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                handle_escape(app);
            } else if (sc == app->settings.keybind[KEYBIND_WIRE] && no_ctrl) {
                set_active_tool(app, TOOL_WIRE);
            } else if (sc == app->settings.keybind[KEYBIND_VIA] && no_ctrl) {
                set_active_tool(app, TOOL_VIA);
            } else if (sc == app->settings.keybind[KEYBIND_SELECT] && no_ctrl) {
                set_active_tool(app, TOOL_SELECT);
            } else if (sc == app->settings.keybind[KEYBIND_INPUT] && no_ctrl) {
                set_active_tool(app, TOOL_INPUT);
            } else if (sc == app->settings.keybind[KEYBIND_OUTPUT] && no_ctrl) {
                set_active_tool(app, TOOL_OUTPUT);
            } else if (sc == app->settings.keybind[KEYBIND_ROTATE] && app_pending_place_ic(app) != NULL) {
                /* only meaningful while a placement (Components-menu pick or
                   Ctrl+C paste) is actually pending - see place_rotation in
                   app.h. Not a structural edit itself (nothing's been placed
                   yet), so no push_undo here; the placed component's baked-in
                   rotation is what the eventual undo snapshot covers. */
                app->place_rotation = (app->place_rotation + 1) & 3;
            } else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
                /* picks which layer new wires route on - a plain field
                   assignment, not structural, so no undo_push (see
                   circuit_add_wire's layer_slot param) */
                int pos = sc - SDL_SCANCODE_1;
                if (pos < app->circuit.layer_order_count) {
                    app->active_layer_slot = app->circuit.layer_order[pos];
                }
            } else if (sc == app->settings.keybind[KEYBIND_COPY] && (event->key.keysym.mod & KMOD_CTRL)) {
                copy_selection(app);
            } else if (sc == app->settings.keybind[KEYBIND_PASTE] && (event->key.keysym.mod & KMOD_CTRL)) {
                paste_clipboard(app);
            } else if (sc == app->settings.keybind[KEYBIND_UNDO] && (event->key.keysym.mod & KMOD_CTRL)) {
                if (event->key.keysym.mod & KMOD_SHIFT) perform_redo(app); /* Ctrl+Shift+<undo key> */
                else perform_undo(app);
            } else if (sc == app->settings.keybind[KEYBIND_REDO] && (event->key.keysym.mod & KMOD_CTRL)) {
                perform_redo(app);
            } else if (sc == app->settings.keybind[KEYBIND_SAVE] && (event->key.keysym.mod & KMOD_CTRL)) {
                app_save_current(app); /* same fall-through-to-Save-As-if-untitled behavior as the File menu's own Save */
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
            /* mirrors the exact same "keyboard is owned elsewhere" gates
               SDL_KEYDOWN above already checks before ever reaching its own
               Shift/Ctrl chord handling (Settings modal open, layer rename
               field, Section/Text-Label edit in progress) - without this,
               releasing Shift while e.g. typing a capital letter into a
               label would still run the logic below (only the KEYDOWN side
               was gated), which - if the all-layers preview happened to
               already be locked on from earlier, unrelated to this edit -
               would silently unlock it out from under the user just because
               a Shift key-up occurred while text input was capturing every
               other key. */
            if (app->settings_panel.open || layer_panel_is_editing(&app->layer_panel) ||
                app->canvas_edit_kind != CANVAS_EDIT_NONE) {
                break;
            }
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
            if (settings_panel_is_capturing_key(&app->settings_panel)) {
                settings_panel_text_input(&app->settings_panel, event->text.text);
            } else if (layer_panel_is_editing(&app->layer_panel)) {
                layer_panel_text_input(&app->layer_panel, event->text.text);
            } else if (app->canvas_edit_kind != CANVAS_EDIT_NONE) {
                int max_len = canvas_edit_max_len(app->canvas_edit_kind);
                for (const char *p = event->text.text; *p != '\0' && app->canvas_edit_len < max_len; p++) {
                    app->canvas_edit_buf[app->canvas_edit_len++] = *p;
                }
                app->canvas_edit_buf[app->canvas_edit_len] = '\0';
            }
            break;

        default:
            break;
    }
}
