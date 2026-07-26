#ifndef ISONIC_SETTINGS_H
#define ISONIC_SETTINGS_H

#include <SDL2/SDL.h>

/* The keyboard shortcuts that make sense to rebind - found in
   input_handler.c's SDL_KEYDOWN case. Deliberately excludes the 1-9
   layer-select keys (inherently positional), Delete/Backspace/Escape
   (universal), and the Shift/Ctrl+Shift layer-preview-lock chord (a
   modifier chord, not a single rebindable key). */
/* Ordered to match the Settings popup's listing: tool-switch keys first (in
   the same left-to-right order as their taskbar buttons), then edit actions,
   then Save last. Purely a display/iteration order - each entry is still
   saved under its own named key (see k_action_keys in settings.c), so
   reordering this enum never breaks an existing settings.ini. */
typedef enum {
    KEYBIND_SELECT,
    KEYBIND_WIRE,
    KEYBIND_VIA,
    KEYBIND_INPUT,
    KEYBIND_OUTPUT,
    KEYBIND_TEXT_LABEL,
    KEYBIND_COPY,
    KEYBIND_PASTE,
    KEYBIND_UNDO,
    KEYBIND_REDO,
    KEYBIND_ROTATE,
    KEYBIND_SAVE,
    KEYBIND_ACTION_COUNT
} KeybindAction;

typedef struct {
    int autosave_minutes;        /* 0 = disabled */
    int layer_panel_anchor_left; /* 0 = right (today's fixed behavior), 1 = left */
    int diag_chips_enabled;      /* 0 hides the bottom-left error/warning chip stack entirely */
    int diag_hover_enabled;      /* 0 suppresses the description tooltip shown while hovering a
                                     chip or a flagged wire/pin on the canvas - the colored
                                     highlight dots themselves (render_diagnostic_highlights)
                                     stay regardless, only the text popup is gated by this */
    int wire_drag_detach;        /* 0 = Stay Connected (today's fixed behavior): dragging a
                                     component/wire/selection also drags along any unselected
                                     wire endpoint or via sitting on one of its anchor points,
                                     even if that stretches the dragged-along wire into a
                                     diagonal. 1 = Detach: only the selected item(s) move: nothing
                                     else is dragged along, so anything that was attached is left
                                     behind at its old position - "unplugging" the connection
                                     instead of stretching it. See snapshot_drag_attachments in
                                     input_handler.c, the single choke point this gates. */
    /* SDL_Keycode, not SDL_Scancode - these are letter mnemonics (Undo=Z,
       Wire=W, ...), so they must follow the character the user's actual
       keyboard layout produces, not a physical key position. A scancode is
       layout-independent (based on the US QWERTY reference), which silently
       swaps Undo/Redo on any QWERTZ (German/Swiss) layout, where Y and Z
       physically trade places relative to QWERTY - Settings would keep
       showing "Ctrl+Z" while the key that produces "Z" no longer matched. */
    SDL_Keycode keybind[KEYBIND_ACTION_COUNT];
} Settings;

/* Row label for the Settings popup's Keybind section, e.g. "Wire Tool". */
const char *keybind_action_label(KeybindAction action);

/* Today's hardcoded keycodes (W/F/Space/Q/E/Ctrl+C/Ctrl+V/Ctrl+Z/Ctrl+Y/R/
   Ctrl+S/T), autosave off, Layers panel on the right - a user who never opens
   Settings sees zero behavior change from before this feature existed. */
void settings_defaults(Settings *settings);

/* Loads from %APPDATA%\Isonic\settings.ini (see platform_settings_path in
   platform_win32.h). Falls back to settings_defaults for a missing file, a
   missing/malformed individual key, or any other read failure - never
   partially-applies a corrupt file over otherwise-good defaults. */
void settings_load(Settings *settings);
/* Returns 1 on success, 0 if the file couldn't be written. */
int settings_save(const Settings *settings);

#endif
