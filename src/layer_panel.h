#ifndef ISONIC_LAYER_PANEL_H
#define ISONIC_LAYER_PANEL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "taskbar.h"  /* TASKBAR_HEIGHT */

/* Always-visible panel, top-right below the taskbar, listing the circuit's
   layers in stack order: color swatch, number (matches the 1-9 routing
   keybind, see input_handler.c), name, reorder arrows, delete. Its palette
   and per-row delete button deliberately mirror data_editor.c's own style
   (colors, close-X stroke) so the two panels read as one consistent system
   rather than two different ad-hoc widgets - see layer_panel.c. Also owns
   the two small popups it needs - a preset color picker, and the only real
   text-input field anywhere in Isonic (for naming a layer) - kept
   deliberately minimal (click to focus, type, backspace, Enter/click-away
   confirms, Escape cancels), not a general text system. */
typedef struct {
    /* which layer_order position's color swatch popup is open, -1 if none */
    int color_popup_for;
    SDL_Rect color_popup_rect;

    /* which layer_order position's name is being edited, -1 if none;
       LAYER_PANEL_EDITING_NEW means the "+ Add Layer" field itself */
    int editing_pos;
    char edit_buffer[24];
    int edit_len;

    /* Geometry from the last render call, reused by the click handler for
       hit-testing - same pattern data_editor.c/taskbar.c already use. */
    SDL_Rect panel_rect;
    SDL_Rect row_rects[MAX_LAYERS];
    SDL_Rect swatch_rects[MAX_LAYERS];
    SDL_Rect name_rects[MAX_LAYERS];
    SDL_Rect up_rects[MAX_LAYERS];
    SDL_Rect down_rects[MAX_LAYERS];
    SDL_Rect delete_rects[MAX_LAYERS];
    SDL_Rect add_row_rect;
} LayerPanel;

/* Longest a layer name can be typed as (see layer_panel_text_input) - keeps
   both the panel's dynamic width and the name column itself from growing
   without bound. Layer.name's own buffer (circuit.h) is sized comfortably
   above this so the limit lives here, at the one place names are ever
   actually typed, rather than as a silent truncation deep in circuit.c. */
#define LAYER_NAME_MAX_LEN 20

#define LAYER_PANEL_EDITING_NEW (-2)

typedef enum {
    LAYER_PANEL_CLICK_MISS,      /* landed outside the panel/popups entirely */
    LAYER_PANEL_CLICK_CONSUMED,  /* landed on the panel but changed nothing structural (e.g. opened a popup) */
    LAYER_PANEL_CLICK_CHANGED,   /* changed circuit structure (add/remove/reorder/recolor/rename) - caller should undo_push */
} LayerPanelClickResult;

void layer_panel_init(LayerPanel *lp);

/* window_w/panel_right_margin position the panel's right edge -
   panel_right_margin is 0 normally, or the Manage Data panel's own width
   when that's open, so the two sit side by side instead of overlapping
   (see app.c). active_layer_slot is highlighted so it's clear which layer
   new wires land on. The panel's width grows to fit its longest current
   layer name (and whatever's being typed right now) rather than clipping
   or squeezing text under the buttons - see layer_panel.c. */
void layer_panel_render(SDL_Renderer *renderer, TTF_Font *font, LayerPanel *lp, const Circuit *circuit,
                         int active_layer_slot, int window_w, int panel_right_margin, int hover_x, int hover_y);

/* True if (x,y) falls on the panel or either open popup - used to swallow
   clicks there instead of letting them act on the circuit underneath, same
   role as taskbar_covers_point/data_editor_covers_point. */
int layer_panel_covers_point(const LayerPanel *lp, int x, int y);

/* Handles a left click at (x,y): swatch/popup color pick, name click (a
   plain click just selects that layer, same as clicking anywhere else on
   its row - double_click is what actually starts editing the name, so a
   single click never interrupts you mid-read of a name you didn't mean to
   rename), reorder arrows, delete, or the add-layer row. active_layer_slot
   is set here on any row click (not just name), in addition to number keys
   owning it (see input_handler.c) - except it's left alone, not read, if
   the active layer itself gets deleted, in which case this points it at
   whatever's left. */
LayerPanelClickResult layer_panel_handle_click(LayerPanel *lp, Circuit *circuit, int *active_layer_slot, int x, int y,
                                                int double_click);

/* True while a name is being typed - input_handler.c routes SDL_TEXTINPUT/
   backspace/Enter/Escape here instead of to number-key layer selection or
   any other shortcut while this is active, and keeps SDL_StartTextInput/
   SDL_StopTextInput in sync with it. */
int layer_panel_is_editing(const LayerPanel *lp);
/* Appends typed text up to LAYER_NAME_MAX_LEN characters - anything past
   that is silently dropped, same as a native text field with a maxlength. */
void layer_panel_text_input(LayerPanel *lp, const char *text);
/* MISS if not currently editing; CONSUMED for Backspace/Escape/typing;
   CHANGED if Enter committed a non-empty rename/new-layer name (caller
   should undo_push). */
LayerPanelClickResult layer_panel_handle_key(LayerPanel *lp, Circuit *circuit, SDL_Scancode sc);

#endif
