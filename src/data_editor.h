#ifndef ISONIC_DATA_EDITOR_H
#define ISONIC_DATA_EDITOR_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "taskbar.h"

/* "Manage Data" panel: lets the user directly view/edit an AT28C64B's
   memory content (address -> byte) without wiring up address/data/control
   pins in the circuit at all - a much faster way to seed ROM content than
   toggling Input wires one bit at a time through the pins. Only offered
   when the circuit's current selection is exactly one component and it's an
   AT28C64B-15PU (see data_editor_eligible) - same "singular, unambiguous
   target" rule Ctrl+C copy already uses for its own component lookup. */
typedef struct {
    int open;
    /* which instance the panel is bound to, NULL if closed. Set once when
       the panel is opened (data_editor_handle_click) and left alone after
       that regardless of what the circuit's current selection does - the
       panel is meant to stay open and usable while the user clicks around
       the canvas, only closing via Escape or the panel's own X button (see
       app.h's handle_escape) or if this component gets deleted. */
    Component *editing;
    int scroll_row; /* first visible address */

    /* Geometry computed by the last data_editor_render call, reused by the
       click/wheel handlers below for hit-testing - same "compute once, reuse
       for hit-testing" pattern render.c's render_wire_terminal_bounds uses,
       so rendering and input can never disagree about where something is. */
    SDL_Rect button_rect;
    SDL_Rect panel_rect;
    SDL_Rect close_rect;
    SDL_Rect body_rect;
    SDL_Rect scrollbar_rect; /* the whole track, right edge of the panel */
    SDL_Rect thumb_rect;     /* the draggable handle within the track */
    int col_bits_x;
    int row_h;
    int visible_rows;

    /* set while the scrollbar thumb is being dragged (see
       data_editor_handle_click/_motion/_release) - a plain click on the
       track (not a drag) jumps straight to that position too, so this only
       needs to track "is the mouse button still held from that click". */
    int dragging_scrollbar;
} DataEditor;

void data_editor_init(DataEditor *de);

/* The circuit's sole current selection, if (and only if) it's an
   AT28C64B-15PU - NULL otherwise (nothing selected, more than one thing
   selected, a wire selected, or a component selected that isn't this IC). */
Component *data_editor_eligible(Circuit *circuit);

/* Draws the "Manage Data" button just right of the taskbar's Components
   button (only if eligible != NULL - this only gates the button, not the
   panel below) and, if open, the scrollable address/data panel along the
   right edge of the window, showing de->editing's content regardless of
   what eligible currently is. Only closes on its own if de->editing was
   deleted from the circuit out from under it - anything else (selection
   changing) leaves an already-open panel alone. */
void data_editor_render(SDL_Renderer *renderer, TTF_Font *font, DataEditor *de, Component *eligible,
                         const Taskbar *tb, int window_w, int window_h, int hover_x, int hover_y);

/* True if (x,y) falls on the Manage Data button or (if open) the panel -
   used to swallow clicks/wheel there instead of letting them act on the
   circuit/camera underneath, same role as taskbar_covers_point. */
int data_editor_covers_point(const DataEditor *de, int x, int y);

/* Handles a left click at (x,y): toggles the button open/closed (binding
   the panel to eligible when it opens), or - while open, regardless of
   eligible - the panel's own X close button, its scrollbar (jumps to that
   position and starts a drag), or one of its data bits. Returns 1 if the
   click was consumed, so the caller doesn't also treat it as a canvas
   click. */
int data_editor_handle_click(DataEditor *de, Component *eligible, int x, int y);

/* Continues a scrollbar drag started by data_editor_handle_click - call on
   every mouse-motion event regardless of button state, it no-ops unless
   dragging_scrollbar is set. */
void data_editor_handle_motion(DataEditor *de, int y);

/* Ends a scrollbar drag - call on left-button mouse-up regardless of
   whether one was in progress. */
void data_editor_handle_release(DataEditor *de);

/* Scrolls the panel if (x,y) is over it. Returns 1 if consumed, in which
   case the caller should skip its own wheel handling (camera zoom). */
int data_editor_handle_wheel(DataEditor *de, int x, int y, int wheel_y);

#endif
