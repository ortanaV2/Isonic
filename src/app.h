#ifndef ISONIC_APP_H
#define ISONIC_APP_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "camera.h"
#include "taskbar.h"
#include "diagnostics.h"
#include "data_editor.h"
#include "layer_panel.h"

#define MAX_DRAG_ATTACHMENTS 64
/* Screen-pixel hit-testing tolerance shared by input_handler.c (click/select) and
   app.c (the temporary "you'd connect here" highlight while dragging a wire). */
#define WIRE_HIT_TOLERANCE_PX 6.0f

/* What a Select-mode left-drag is currently moving. */
typedef enum {
    DRAG_NONE,
    DRAG_COMPONENT,  /* moving a whole IC body, see selected_component_id */
    DRAG_WIRE_BODY,  /* moving a whole wire (both endpoints), see drag_wire_id */
    DRAG_WIRE_NODE,  /* moving every wire endpoint coincident at one point */
    DRAG_SELECTION   /* moving every currently-.selected component/wire together, e.g. after a marquee select */
} DragKind;

typedef struct {
    Circuit circuit;
    Camera camera;
    Taskbar taskbar;
    TTF_Font *font;       /* taskbar only, loaded at display size */
    TTF_Font *font_large; /* all component/wire labels, loaded oversized and scaled down for crisp zoom */

    Tool active_tool;
    /* which IC TOOL_PLACE_IC would place, chosen from the taskbar's
       Components dropdown - NULL until something's been picked. Points at a
       static string literal owned by taskbar.c's menu catalog, so no
       lifetime/ownership concerns. */
    const char *place_ic_name;

    int window_w, window_h;
    int running;

    /* selection (mutually exclusive) */
    int selected_component_id; /* -1 = none */
    int selected_wire_id;      /* -1 = none */

    /* dragging with the left mouse button in Select mode - a component body,
       a whole wire body, or a wire node (every endpoint coincident at one
       point); see DragKind above */
    DragKind drag_kind;
    int drag_last_gx, drag_last_gy; /* previous frame's cursor grid cell, for computing per-frame deltas */
    int drag_wire_id;               /* which wire, only for DRAG_WIRE_BODY */

    /* DRAG_SELECTION: which single component/wire was actually clicked to
       start the drag (-1/-1 if neither applies), and whether the drag ever
       moved anything. A plain click-and-release on an item that happens to
       be part of a bigger existing selection should still collapse the
       selection down to just that one item, same as clicking any other
       unselected item always has - only an actual drag should preserve and
       move the whole group. See finish_drag. */
    int drag_click_component_id, drag_click_wire_id;
    int drag_moved;

    /* DRAG_COMPONENT/DRAG_WIRE_BODY: wires whose endpoint coincided with one
       of the dragged thing's anchor points (pins, or the wire's own two ends)
       at drag-start; shifted by the same per-frame delta so moving something
       doesn't silently tear its connections (see plan Revision 1) */
    int drag_attach_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_wire_end[MAX_DRAG_ATTACHMENTS]; /* 0 = from, 1 = to */
    int drag_attach_count;

    /* Vias behave like a tiny component pinned to a wire node - any via
       whose point coincides with one of the dragged thing's anchor points
       at drag-start moves along with it too, same idea as
       drag_attach_wire_id above but for a via (which has no "end", just
       one point) - see snapshot_drag_attachments/apply_drag_attachments. */
    int drag_attach_via_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_via_count;

    /* DRAG_WIRE_NODE: every wire endpoint exactly at the grabbed point, which
       all move together as "the node" - also temporarily marked .selected so
       they're visibly highlighted for the duration of the drag */
    int drag_node_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_node_wire_end[MAX_DRAG_ATTACHMENTS];
    int drag_node_count;

    /* any via sitting exactly at the grabbed node moves along with it too -
       see begin_wire_node_drag/DRAG_WIRE_NODE in input_handler.c */
    int drag_node_via_id[MAX_DRAG_ATTACHMENTS];
    int drag_node_via_count;

    /* panning the camera with the middle mouse button */
    int panning;

    /* rubber-band multi-select: a Select-mode left-drag started over empty
       space (no node/component/wire under the cursor) draws a box, and on
       release every component/wire fully enclosed by it - not merely touched
       - gets marked .selected, same as Windows Explorer. Screen pixels, not
       grid coords: the box is a pure screen-space overlay, so it's simplest
       to keep it in the same space it's drawn in and only convert to grid
       space once, at release, for the containment test. */
    int marquee_active;
    int marquee_start_mx, marquee_start_my;
    int marquee_cur_mx, marquee_cur_my;

    /* Ctrl+C copy: copies a component and immediately starts a
       placement-at-cursor preview for it, same click-to-place interaction as
       choosing an IC from the Components dropdown (see app_pending_place_ic)
       but not tied to a menu selection. clipboard_ic_def is left set after
       pasting ends (harmless - it just points at static IC_Def data owned by
       the registry); pasting is what actually gates the preview/placement
       behavior. */
    const IC_Def *clipboard_ic_def;
    int pasting;

    /* dragging out a new wire between two arbitrary grid points - kind tags it
       as a plain wire or an Input/Output terminal (see WireKind in circuit.h) */
    int wiring;
    WireKind wiring_kind;
    int wire_from_gx, wire_from_gy;
    int wire_cursor_gx, wire_cursor_gy;

    /* recomputed every frame in app_update, right after sim_step so it sees
       this frame's settled pin values - see diagnostics.h */
    DiagnosticSet diagnostics;

    /* "Manage Data" EEPROM content editor - see data_editor.h */
    DataEditor data_editor;

    /* Multi-layer routing - see layer_panel.h/circuit.h. active_layer_slot
       (a circuit->layers[] slot index, not a layer_order[] position) is
       which layer a newly-drawn wire is placed on; the 1-9 keys and the
       panel's row clicks both just set this one field. */
    LayerPanel layer_panel;
    int active_layer_slot;

    /* Shift-hold previews every wire in its own layer's color instead of
       the plain gray/green signal color (see render_circuit's
       layer_preview param); Ctrl+Shift toggles that preview permanently on
       (CapsLock-like) until Ctrl+Shift again or a lone Shift tap.
       shift_press_was_chord distinguishes "Shift held as part of a
       Ctrl+Shift chord" from "a plain Shift tap/hold" so release only
       un-locks in the latter case - see input_handler.c. */
    int shift_held;
    int layer_preview_locked;
    int shift_press_was_chord;
} App;

void app_init(App *app, int window_w, int window_h);
void app_shutdown(App *app);

/* Defined in input_handler.c */
void app_handle_event(App *app, const SDL_Event *event);

void app_update(App *app);
/* Draws one frame onto whatever render target is currently bound. Does not
   call SDL_RenderPresent - the caller (main.c) owns that, since it may render
   into an offscreen texture first for supersampled anti-aliasing. */
void app_render(App *app, SDL_Renderer *renderer);

/* The IC that would be placed by a click right now - either the one chosen
   from the taskbar's Components dropdown (TOOL_PLACE_IC, see place_ic_name
   above) or an in-progress Ctrl+C paste (see pasting above) - or NULL if
   neither applies. Centralizes which source wins so preview/footprint/
   placement code never needs to care which one it is. */
const IC_Def *app_pending_place_ic(const App *app);

/* WIRE_HIT_TOLERANCE_PX converted to grid units at the current zoom - the
   tolerance every wire/pin proximity hit-test in input_handler.c and app.c uses. */
float app_wire_hit_tolerance(const App *app);

#endif
