#ifndef ISONIC_SETTINGS_PANEL_H
#define ISONIC_SETTINGS_PANEL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "settings.h"

/* The centered-modal "Settings" popup - autosave frequency, which corner the
   Layers panel docks to, and interactive keybind rebinding, plus Save/Reset
   Default. Unlike layer_panel.h/data_editor.h (docked side panels, always
   visible or toggled in place) this is a true modal: while open it sits over
   a dimming scrim and swallows the whole window (see
   settings_panel_covers_point) - nothing behind it reacts to the cursor at
   all, including hover highlights, other panels' buttons, and the canvas
   itself. Closes only via its own X or Escape - a click outside the modal
   body is fully swallowed but deliberately does NOT close it.

   Edits here go into the panel's own `working` copy, NOT directly into the
   live App.settings (`live`) - toggling Corner or rebinding a key updates
   what the popup itself displays, but has no effect on the running app yet.
   Clicking Save is what actually copies `working` into `*live` (making it
   take effect immediately - e.g. the Layers panel visibly jumps to its new
   corner right then) and persists it to disk; Reset Default only resets
   `working` back to defaults, same deal - still not live until Save. Opening
   the popup (settings_panel_open) always reseeds `working` from whatever's
   currently live, and closing it without saving simply discards `working` -
   next open starts fresh from the live settings again. */
typedef struct {
    int open;
    Settings *live;
    Settings working;

    int autosave_editing;
    char autosave_edit_buf[8];
    int autosave_edit_len;

    /* which KeybindAction is currently listening for its next keypress,
       -1 if none - see settings_panel_handle_key. */
    int capturing_action;

    /* geometry from the last render call, reused by the click handler - same
       pattern layer_panel.c/data_editor.c already use. */
    SDL_Rect modal_rect;
    SDL_Rect close_rect;
    SDL_Rect autosave_field_rect;
    SDL_Rect corner_left_rect, corner_right_rect;
    SDL_Rect diag_chips_on_rect, diag_chips_off_rect;
    SDL_Rect diag_hover_on_rect, diag_hover_off_rect;
    SDL_Rect keybind_box_rects[KEYBIND_ACTION_COUNT];
    SDL_Rect save_button_rect;
    SDL_Rect reset_button_rect;
} SettingsPanel;

typedef enum {
    SETTINGS_PANEL_CLICK_MISS,
    SETTINGS_PANEL_CLICK_CONSUMED,
} SettingsPanelClickResult;

/* live is the App's actual live Settings (App.settings) - held for the
   panel's whole lifetime, not re-bound per open; only ever written by Save. */
void settings_panel_init(SettingsPanel *sp, Settings *live);
/* Opens the popup and reseeds its working copy from *live - called when the
   taskbar's Settings button is clicked. */
void settings_panel_open(SettingsPanel *sp);

void settings_panel_render(SDL_Renderer *renderer, TTF_Font *font, SettingsPanel *sp, int window_w, int window_h,
                            int hover_x, int hover_y);

/* True while the popup is open at all - it's fully modal (dims/covers the
   whole window), so this should gate essentially everything else in
   app.c/input_handler.c while true, same role taskbar_covers_point/
   data_editor_covers_point/layer_panel_covers_point play for their own
   panels. */
int settings_panel_covers_point(const SettingsPanel *sp, int x, int y);

/* Handles a left click. MISS if the popup isn't open at all. Closing (X, or
   anywhere outside the modal body) sets sp->open = 0 and returns CONSUMED -
   see the struct's own comment on why that discards `working` rather than
   applying it. */
SettingsPanelClickResult settings_panel_handle_click(SettingsPanel *sp, int x, int y);

/* True while a keybind capture or the autosave field is intercepting
   keyboard input - input_handler.c must check this before its own shortcut
   dispatch, same role layer_panel_is_editing already plays. */
int settings_panel_is_capturing_key(const SettingsPanel *sp);
/* Routes SDL_TEXTINPUT while the autosave field is focused - digits only,
   non-digit characters are silently dropped. */
void settings_panel_text_input(SettingsPanel *sp, const char *text);
/* Routes SDL_KEYDOWN while capturing a keybind or editing the autosave
   field. A captured scancode rebinds that action, swapping with whatever
   other action previously held that key (if any); Escape cancels either
   without changing anything; Enter/Backspace behave like layer_panel's own
   rename field for the autosave box. */
void settings_panel_handle_key(SettingsPanel *sp, SDL_Scancode sc);

#endif
