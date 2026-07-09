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

    /* dragging the selected component with the left mouse button */
    int dragging;
    int drag_offset_x, drag_offset_y; /* grid offset from component origin to cursor */

    /* wires whose endpoint coincided with one of the dragged component's pins
       at drag-start; followed along for the duration of the drag so moving a
       component doesn't silently tear its connections (see plan Revision 1) */
    int drag_attach_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_wire_end[MAX_DRAG_ATTACHMENTS]; /* 0 = from, 1 = to */
    int drag_attach_pin_index[MAX_DRAG_ATTACHMENTS];
    int drag_attach_count;

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

#endif
