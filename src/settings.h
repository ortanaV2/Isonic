#ifndef ISONIC_SETTINGS_H
#define ISONIC_SETTINGS_H

#include <SDL2/SDL.h>

/* The keyboard shortcuts that make sense to rebind - found in
   input_handler.c's SDL_KEYDOWN case. Deliberately excludes the 1-9
   layer-select keys (inherently positional), Delete/Backspace/Escape
   (universal), and the Shift/Ctrl+Shift layer-preview-lock chord (a
   modifier chord, not a single rebindable key). */
typedef enum {
    KEYBIND_WIRE,
    KEYBIND_VIA,
    KEYBIND_SELECT,
    KEYBIND_INPUT,
    KEYBIND_OUTPUT,
    KEYBIND_COPY,
    KEYBIND_UNDO,
    KEYBIND_REDO,
    KEYBIND_ROTATE,
    KEYBIND_ACTION_COUNT
} KeybindAction;

typedef struct {
    int autosave_minutes;        /* 0 = disabled */
    int layer_panel_anchor_left; /* 0 = right (today's fixed behavior), 1 = left */
    SDL_Scancode keybind[KEYBIND_ACTION_COUNT];
} Settings;

/* Row label for the Settings popup's Keybind section, e.g. "Wire Tool". */
const char *keybind_action_label(KeybindAction action);

/* Today's hardcoded scancodes (W/V/Space/Q/E/Ctrl+C/Ctrl+Z/Ctrl+Y/R), autosave
   off, Layers panel on the right - a user who never opens Settings sees zero
   behavior change from before this feature existed. */
void settings_defaults(Settings *settings);

/* Loads from %APPDATA%\Isonic\settings.ini (see platform_settings_path in
   platform_win32.h). Falls back to settings_defaults for a missing file, a
   missing/malformed individual key, or any other read failure - never
   partially-applies a corrupt file over otherwise-good defaults. */
void settings_load(Settings *settings);
/* Returns 1 on success, 0 if the file couldn't be written. */
int settings_save(const Settings *settings);

#endif
