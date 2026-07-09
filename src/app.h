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

/* Footprint (grid cells) of the IC the given tool would place; only meaningful
   for TOOL_PLACE_SN7408 (the only remaining click-to-place tool - Input/Output
   are drag-drawn wires now, see WireKind in circuit.h). */
void app_get_tool_footprint(Tool tool, int *out_w, int *out_h);

/* WIRE_HIT_TOLERANCE_PX converted to grid units at the current zoom - the
   tolerance every wire/pin proximity hit-test in input_handler.c and app.c uses. */
float app_wire_hit_tolerance(const App *app);

#endif
