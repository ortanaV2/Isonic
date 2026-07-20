#ifndef ISONIC_PLATFORM_WIN32_H
#define ISONIC_PLATFORM_WIN32_H

#include <stddef.h>
#include <SDL2/SDL.h>

/* Thin OS-integration boundary - native file dialogs, a native confirm
   messagebox, spawning a second instance of this exe, and locating the
   per-user settings directory. Everything here is Windows-only (this app
   already ships Windows-only, see main.c's existing #ifdef _WIN32 dark-
   titlebar/icon code); the .c file is entirely #ifdef _WIN32, but every
   function below is declared unconditionally with a failing/no-op #else
   stub in the .c so callers (app.c/input_handler.c) never need their own
   #ifdef. All paths handed to/from these functions are plain narrow char*
   (system codepage, CP_ACP) - wide-char conversion happens only inside
   platform_win32.c, nowhere else in the app. */

typedef enum {
    PLATFORM_DISCARD_CANCEL,    /* abort whatever the caller was about to do */
    PLATFORM_DISCARD_SAVE,      /* save first, then proceed */
    PLATFORM_DISCARD_DONT_SAVE  /* proceed without saving */
} PlatformDiscardChoice;

/* Native "Open" file picker filtered to *.isonic. Returns 1 and fills
   out_path if the user picked a file, 0 if they cancelled. */
int platform_open_file_dialog(SDL_Window *window, char *out_path, size_t out_cap);
/* Native "Save As" file picker, same filter, appends .isonic if the user
   didn't type an extension. Returns 1 and fills out_path, 0 if cancelled. */
int platform_save_file_dialog(SDL_Window *window, char *out_path, size_t out_cap);

/* The standard "you have unsaved changes" Save/Don't Save/Cancel prompt. */
PlatformDiscardChoice platform_confirm_discard_changes(SDL_Window *window);
/* A plain OK error box, for a save/load that failed partway through. */
void platform_show_error(SDL_Window *window, const char *message);

/* Launches a second, fully independent instance of this same exe (own
   process, own blank document) - fire and forget, doesn't wait and doesn't
   affect the current window/document at all. */
void platform_spawn_new_instance(void);

/* Fills out with the full path to the per-user settings file
   (%APPDATA%\Isonic\settings.ini), creating the Isonic directory first if
   it doesn't exist yet. Returns 1 on success, 0 if the path couldn't be
   determined or the directory couldn't be created. */
int platform_settings_path(char *out, size_t cap);

#endif
