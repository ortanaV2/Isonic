#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "kv_parser.h"
#include "platform_win32.h"

static const char *k_action_labels[KEYBIND_ACTION_COUNT] = {
    "Wire Tool", "Via Tool", "Select Tool", "Input Tool", "Output Tool", "Copy", "Paste", "Undo", "Redo", "Rotate", "Save",
};

/* File key for each action, in the same order as KeybindAction - shared by
   settings_load/settings_save so the two stay in lockstep by construction. */
static const char *k_action_keys[KEYBIND_ACTION_COUNT] = {
    "keybind_wire", "keybind_via", "keybind_select", "keybind_input", "keybind_output",
    "keybind_copy", "keybind_paste", "keybind_undo", "keybind_redo", "keybind_rotate", "keybind_save",
};

const char *keybind_action_label(KeybindAction action) {
    if (action < 0 || action >= KEYBIND_ACTION_COUNT) return "";
    return k_action_labels[action];
}

void settings_defaults(Settings *settings) {
    settings->autosave_minutes = 0;
    settings->layer_panel_anchor_left = 0;
    settings->keybind[KEYBIND_WIRE] = SDL_SCANCODE_W;
    /* was V - collided with Ctrl+V once Paste got its own keybind below
       (the plain-key checks never excluded Ctrl, so Ctrl+V would also
       switch to the Via tool as a side effect - see input_handler.c's
       Ctrl-guards on every plain tool-switch key, added at the same time). */
    settings->keybind[KEYBIND_VIA] = SDL_SCANCODE_F;
    settings->keybind[KEYBIND_SELECT] = SDL_SCANCODE_SPACE;
    settings->keybind[KEYBIND_INPUT] = SDL_SCANCODE_Q;
    settings->keybind[KEYBIND_OUTPUT] = SDL_SCANCODE_E;
    settings->keybind[KEYBIND_COPY] = SDL_SCANCODE_C;
    settings->keybind[KEYBIND_PASTE] = SDL_SCANCODE_V;
    settings->keybind[KEYBIND_UNDO] = SDL_SCANCODE_Z;
    settings->keybind[KEYBIND_REDO] = SDL_SCANCODE_Y;
    settings->keybind[KEYBIND_ROTATE] = SDL_SCANCODE_R;
    settings->keybind[KEYBIND_SAVE] = SDL_SCANCODE_S;
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

    while (kv_next_line(f, line, sizeof(line))) {
        char key[64], val[256];
        if (!kv_split_kv(line, key, sizeof(key), val, sizeof(val))) continue;

        if (strcmp(key, "autosave_minutes") == 0) {
            settings->autosave_minutes = atoi(val);
        } else if (strcmp(key, "layer_panel_anchor_left") == 0) {
            settings->layer_panel_anchor_left = atoi(val);
        } else {
            for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
                if (strcmp(key, k_action_keys[i]) == 0) {
                    settings->keybind[i] = (SDL_Scancode)atoi(val);
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

    kv_write_line(f, "ISONIC_SETTINGS 1");

    char line[128];
    snprintf(line, sizeof(line), "autosave_minutes=%d", settings->autosave_minutes);
    kv_write_line(f, line);
    snprintf(line, sizeof(line), "layer_panel_anchor_left=%d", settings->layer_panel_anchor_left);
    kv_write_line(f, line);
    for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
        snprintf(line, sizeof(line), "%s=%d", k_action_keys[i], (int)settings->keybind[i]);
        kv_write_line(f, line);
    }

    fclose(f);
    return 1;
}
