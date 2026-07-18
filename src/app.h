#ifndef ISONIC_APP_H
#define ISONIC_APP_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "camera.h"
#include "taskbar.h"

#define MAX_DRAG_ATTACHMENTS 64
/* Screen-pixel hit-testing tolerance shared by input_handler.c (click/select) and
   app.c (the temporary "you'd connect here" highlight while dragging a wire). */
#define WIRE_HIT_TOLERANCE_PX 6.0f

/* What a Select-mode left-drag is currently moving. */
typedef enum {
    DRAG_NONE,
    DRAG_COMPONENT,  /* moving a whole IC body, see selected_component_id */
    DRAG_WIRE_BODY,  /* moving a whole wire (both endpoints), see drag_wire_id */
    DRAG_WIRE_NODE   /* moving every wire endpoint coincident at one point */
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

    /* DRAG_COMPONENT/DRAG_WIRE_BODY: wires whose endpoint coincided with one
       of the dragged thing's anchor points (pins, or the wire's own two ends)
       at drag-start; shifted by the same per-frame delta so moving something
       doesn't silently tear its connections (see plan Revision 1) */
    int drag_attach_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_wire_end[MAX_DRAG_ATTACHMENTS]; /* 0 = from, 1 = to */
    int drag_attach_count;

    /* DRAG_WIRE_NODE: every wire endpoint exactly at the grabbed point, which
       all move together as "the node" - also temporarily marked .selected so
       they're visibly highlighted for the duration of the drag */
    int drag_node_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_node_wire_end[MAX_DRAG_ATTACHMENTS];
    int drag_node_count;

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
