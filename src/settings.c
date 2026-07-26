#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "kv_parser.h"
#include "platform_win32.h"

static const char *k_action_labels[KEYBIND_ACTION_COUNT] = {
    "Select Tool", "Wire Tool", "Via Tool", "Input Tool", "Output Tool", "Text Label Tool",
    "Copy", "Paste", "Undo", "Redo", "Rotate", "Save",
};

/* File key for each action, in the same order as KeybindAction - shared by
   settings_load/settings_save so the two stay in lockstep by construction. */
static const char *k_action_keys[KEYBIND_ACTION_COUNT] = {
    "keybind_select", "keybind_wire", "keybind_via", "keybind_input", "keybind_output", "keybind_text_label",
    "keybind_copy", "keybind_paste", "keybind_undo", "keybind_redo", "keybind_rotate", "keybind_save",
};

const char *keybind_action_label(KeybindAction action) {
    if (action < 0 || action >= KEYBIND_ACTION_COUNT) return "";
    return k_action_labels[action];
}

void settings_defaults(Settings *settings) {
    settings->autosave_minutes = 0;
    settings->layer_panel_anchor_left = 0;
    settings->diag_chips_enabled = 1;
    settings->diag_hover_enabled = 1;
    settings->wire_drag_detach = 0;
    settings->keybind[KEYBIND_SELECT] = SDLK_SPACE;
    settings->keybind[KEYBIND_WIRE] = SDLK_w;
    /* was V - collided with Ctrl+V once Paste got its own keybind below
       (the plain-key checks never excluded Ctrl, so Ctrl+V would also
       switch to the Via tool as a side effect - see input_handler.c's
       Ctrl-guards on every plain tool-switch key, added at the same time). */
    settings->keybind[KEYBIND_VIA] = SDLK_f;
    settings->keybind[KEYBIND_INPUT] = SDLK_q;
    settings->keybind[KEYBIND_OUTPUT] = SDLK_e;
    settings->keybind[KEYBIND_TEXT_LABEL] = SDLK_t;
    settings->keybind[KEYBIND_COPY] = SDLK_c;
    settings->keybind[KEYBIND_PASTE] = SDLK_v;
    settings->keybind[KEYBIND_UNDO] = SDLK_z;
    settings->keybind[KEYBIND_REDO] = SDLK_y;
    settings->keybind[KEYBIND_ROTATE] = SDLK_r;
    settings->keybind[KEYBIND_SAVE] = SDLK_s;
}

void settings_load(Settings *settings) {
    settings_defaults(settings);

    char path[512];
    if (!platform_settings_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (f == NULL) return; /* first run / no settings file yet - defaults stand */

    char line[KV_MAX_LINE];
    if (!kv_next_line(f, line, sizeof(line)) || strncmp(line, "ISONIC_SETTINGS", 15) != 0) {
        fclose(f);
        return; /* not a recognizable settings file - keep defaults, don't half-apply garbage */
    }
    /* format version 1 stored keybind[] as SDL_Scancode (physical key
       position); version 2 switched to SDL_Keycode (produced character) to
       fix Undo/Redo swapping on QWERTZ layouts - see settings.h. A v1 file's
       raw ints would silently be reinterpreted as the wrong keycodes if
       loaded here, so keybind_* keys below are only honored from v2+; a v1
       file keeps the (correct, keycode-based) settings_defaults() already
       set above for every keybind, same as a first run. */
    int file_version = 0;
    sscanf(line, "ISONIC_SETTINGS %d", &file_version);

    while (kv_next_line(f, line, sizeof(line))) {
        char key[64], val[256];
        if (!kv_split_kv(line, key, sizeof(key), val, sizeof(val))) continue;

        if (strcmp(key, "autosave_minutes") == 0) {
            settings->autosave_minutes = atoi(val);
        } else if (strcmp(key, "layer_panel_anchor_left") == 0) {
            settings->layer_panel_anchor_left = atoi(val);
        } else if (strcmp(key, "diag_chips_enabled") == 0) {
            settings->diag_chips_enabled = atoi(val);
        } else if (strcmp(key, "diag_hover_enabled") == 0) {
            settings->diag_hover_enabled = atoi(val);
        } else if (strcmp(key, "wire_drag_detach") == 0) {
            settings->wire_drag_detach = atoi(val);
        } else if (file_version >= 2) {
            for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
                if (strcmp(key, k_action_keys[i]) == 0) {
                    settings->keybind[i] = (SDL_Keycode)atoi(val);
                    break;
                }
            }
        }
    }
    fclose(f);
}

int settings_save(const Settings *settings) {
    char path[512];
    if (!platform_settings_path(path, sizeof(path))) return 0;
    FILE *f = fopen(path, "w");
    if (f == NULL) return 0;

    kv_write_line(f, "ISONIC_SETTINGS 2");

    char line[128];
    snprintf(line, sizeof(line), "autosave_minutes=%d", settings->autosave_minutes);
    kv_write_line(f, line);
    snprintf(line, sizeof(line), "layer_panel_anchor_left=%d", settings->layer_panel_anchor_left);
    kv_write_line(f, line);
    snprintf(line, sizeof(line), "diag_chips_enabled=%d", settings->diag_chips_enabled);
    kv_write_line(f, line);
    snprintf(line, sizeof(line), "diag_hover_enabled=%d", settings->diag_hover_enabled);
    kv_write_line(f, line);
    snprintf(line, sizeof(line), "wire_drag_detach=%d", settings->wire_drag_detach);
    kv_write_line(f, line);
    for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
        snprintf(line, sizeof(line), "%s=%d", k_action_keys[i], (int)settings->keybind[i]);
        kv_write_line(f, line);
    }

    fclose(f);
    return 1;
}
