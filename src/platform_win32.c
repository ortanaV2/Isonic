#include "platform_win32.h"

#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
#include <SDL2/SDL_syswm.h>

static HWND get_hwnd(SDL_Window *window) {
    if (window == NULL) return NULL;
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) return NULL;
    return wmi.info.win.window;
}

static void narrow_to_wide(const char *narrow, wchar_t *wide, int wide_cap) {
    MultiByteToWideChar(CP_ACP, 0, narrow, -1, wide, wide_cap);
}

static void wide_to_narrow(const wchar_t *wide, char *narrow, int narrow_cap) {
    WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, narrow_cap, NULL, NULL);
}

int platform_open_file_dialog(SDL_Window *window, char *out_path, size_t out_cap) {
    wchar_t wpath[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = get_hwnd(window);
    ofn.lpstrFilter = L"Isonic Schematic (*.isonic)\0*.isonic\0All Files\0*.*\0";
    ofn.lpstrFile = wpath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"isonic";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return 0;
    wide_to_narrow(wpath, out_path, (int)out_cap);
    return 1;
}

int platform_save_file_dialog(SDL_Window *window, char *out_path, size_t out_cap) {
    wchar_t wpath[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = get_hwnd(window);
    ofn.lpstrFilter = L"Isonic Schematic (*.isonic)\0*.isonic\0All Files\0*.*\0";
    ofn.lpstrFile = wpath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"isonic";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return 0;
    wide_to_narrow(wpath, out_path, (int)out_cap);
    return 1;
}

PlatformDiscardChoice platform_confirm_discard_changes(SDL_Window *window) {
    int r = MessageBoxW(get_hwnd(window),
                         L"You have unsaved changes. Save before continuing?",
                         L"Isonic", MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDYES) return PLATFORM_DISCARD_SAVE;
    if (r == IDNO) return PLATFORM_DISCARD_DONT_SAVE;
    return PLATFORM_DISCARD_CANCEL;
}

void platform_show_error(SDL_Window *window, const char *message) {
    wchar_t wmsg[1024];
    narrow_to_wide(message, wmsg, 1024);
    MessageBoxW(get_hwnd(window), wmsg, L"Isonic", MB_OK | MB_ICONERROR);
}

void platform_spawn_new_instance(void) {
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0) return;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(exe_path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

int platform_settings_path(char *out, size_t cap) {
    const char *appdata = getenv("APPDATA");
    if (appdata == NULL) return 0;

    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\Isonic", appdata);
    CreateDirectoryA(dir, NULL); /* fine if it already exists */

    int n = snprintf(out, cap, "%s\\settings.ini", dir);
    return n > 0 && (size_t)n < cap;
}

#else /* !_WIN32 - Isonic only ships on Windows today; these keep app.c/input_handler.c #ifdef-free */

int platform_open_file_dialog(SDL_Window *window, char *out_path, size_t out_cap) {
    (void)window; (void)out_path; (void)out_cap;
    return 0;
}

int platform_save_file_dialog(SDL_Window *window, char *out_path, size_t out_cap) {
    (void)window; (void)out_path; (void)out_cap;
    return 0;
}

PlatformDiscardChoice platform_confirm_discard_changes(SDL_Window *window) {
    (void)window;
    return PLATFORM_DISCARD_DONT_SAVE;
}

void platform_show_error(SDL_Window *window, const char *message) {
    (void)window; (void)message;
}

void platform_spawn_new_instance(void) {
}

int platform_settings_path(char *out, size_t cap) {
    (void)out; (void)cap;
    return 0;
}

#endif
