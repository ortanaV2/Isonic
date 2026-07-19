#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#endif
#include "app.h"
#include "ic_registry.h"
#include "ics/at28c64b.h"
#include "ics/cd4555.h"
#include "ics/cd74hc04.h"
#include "ics/cd74hct238.h"
#include "ics/sn74hc00.h"
#include "ics/sn74hc02.h"
#include "ics/sn74hc08.h"
#include "ics/sn74hc32.h"
#include "ics/sn74hc86.h"
#include "ics/sn74hc151.h"
#include "ics/sn74hc153.h"
#include "ics/sn74hc244.h"
#include "ics/sn74hc4040.h"
#include "ics/tc74hc373.h"
#include "ics/tlc555.h"

#define WINDOW_W 1280
#define WINDOW_H 800
#define TARGET_FRAME_MS 16

/* Everything is drawn into an offscreen texture at SSAA_SCALE times the
   window's pixel size, then downscaled with linear filtering onto the real
   window - a simple supersampling anti-alias pass, since SDL2's software/GPU
   line and polygon primitives have no AA of their own. */
#define SSAA_SCALE 2

static SDL_Texture *create_scene_target(SDL_Renderer *renderer, int window_w, int window_h) {
    return SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                              window_w * SSAA_SCALE, window_h * SSAA_SCALE);
}

#ifdef _WIN32
/* Windows draws its own title bar (drag, Aero Snap, min/max/close) - this
   just asks DWM to paint that native chrome dark instead of replacing it
   with a custom one, so window management keeps working exactly as before.
   20 is DWMWA_USE_IMMERSIVE_DARK_MODE on Windows 10 20H1+/11; older headers
   that only know the pre-20H1 attribute number (19) get a fallback attempt
   in case the newer one is rejected on an older Windows 10 build. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* DwmSetWindowAttribute alone is unreliable on plain Windows 10 (partial/
   delayed repaints, exactly what was seen without this) unless the app also
   tells uxtheme.dll it's dark-mode-aware first. That's only exposed as
   undocumented ordinal exports (no header, no import lib entries by name),
   so they're resolved manually via GetProcAddress - the same approach every
   other dark-titlebar-on-Win10 implementation (Windows Terminal, VSCode,
   etc.) uses, since Microsoft has never published a public API for it. */
typedef enum { APPMODE_DEFAULT, APPMODE_ALLOWDARK, APPMODE_FORCEDARK, APPMODE_FORCELIGHT, APPMODE_MAX } PreferredAppMode;
typedef PreferredAppMode(WINAPI *SetPreferredAppModeFn)(PreferredAppMode);
typedef BOOL(WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
typedef void(WINAPI *RefreshImmersiveColorPolicyStateFn)(void);

static void enable_dark_titlebar(SDL_Window *window) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) return;
    HWND hwnd = wmi.info.win.window;

    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme != NULL) {
        /* GetProcAddress returns FARPROC - casting straight to a differently-
           shaped function pointer trips -Wcast-function-type under -Wextra,
           so go through void* first, same as any other function-pointer cast
           between incompatible signatures. */
        SetPreferredAppModeFn SetPreferredAppMode_ =
            (SetPreferredAppModeFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
        AllowDarkModeForWindowFn AllowDarkModeForWindow_ =
            (AllowDarkModeForWindowFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(133));
        RefreshImmersiveColorPolicyStateFn RefreshImmersiveColorPolicyState_ =
            (RefreshImmersiveColorPolicyStateFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(104));
        if (SetPreferredAppMode_ != NULL) SetPreferredAppMode_(APPMODE_ALLOWDARK);
        if (RefreshImmersiveColorPolicyState_ != NULL) RefreshImmersiveColorPolicyState_();
        if (AllowDarkModeForWindow_ != NULL) AllowDarkModeForWindow_(hwnd, TRUE);
        FreeLibrary(uxtheme);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);

    BOOL dark = TRUE;
    if (DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)) != S_OK) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }

    /* SWP_FRAMECHANGED alone does NOT reliably repaint the caption on Win10
       (the request gets coalesced away before DWM composes the first frame,
       so the bar stays white until an unrelated event - focus change, or the
       user resizing by hand - happens to force a real non-client repaint).
       Doing an actual 1px grow-then-restore is the geometry change DWM won't
       ignore - it's precisely the resize the user found works by hand, just
       done programmatically and immediately, before the first present, so
       it's never visible. */
    RECT rc;
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/* Loads the same icon.ico already embedded in the exe (see assets/app.rc,
   resource ID 1) and assigns it to the live window via WM_SETICON - so the
   titlebar/Alt-Tab/taskbar icon while running matches the exe's own file
   icon, from a single source file instead of needing a second copy loaded
   through SDL (which has no ICO loader without the SDL2_image dependency
   anyway). Requested at each icon's own system-metric size (SM_CXSMICON for
   the small titlebar icon, SM_CXICON for the large Alt-Tab one) so Windows
   picks the closest matching resolution baked into the .ico instead of
   stretching a single fixed size. */
static void set_window_icon(SDL_Window *window) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) return;
    HWND hwnd = wmi.info.win.window;
    HINSTANCE inst = GetModuleHandleW(NULL);

    HICON small_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                          LR_DEFAULTCOLOR);
    HICON big_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                        LR_DEFAULTCOLOR);
    if (small_icon != NULL) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    if (big_icon != NULL) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big_icon);
}
#endif

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_Window *window = SDL_CreateWindow(
        "Isonic Developer x64",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

#ifdef _WIN32
    /* applied AFTER the renderer exists: creating the D3D renderer touches
       the window's styles/theme, which was stomping the dark non-client
       theming when this ran before it. */
    enable_dark_titlebar(window);
    set_window_icon(window);
#endif

    ic_at28c64b_register();
    ic_cd4555_register();
    ic_cd74hc04_register();
    ic_cd74hct238_register();
    ic_sn74hc00_register();
    ic_sn74hc02_register();
    ic_sn74hc08_register();
    ic_sn74hc32_register();
    ic_sn74hc86_register();
    ic_sn74hc151_register();
    ic_sn74hc153_register();
    ic_sn74hc244_register();
    ic_sn74hc4040_register();
    ic_tc74hc373_register();
    ic_tlc555_register();

    App app;
    app_init(&app, WINDOW_W, WINDOW_H);

    SDL_Texture *scene_target = create_scene_target(renderer, app.window_w, app.window_h);
    int scene_w = app.window_w, scene_h = app.window_h;
    if (scene_target == NULL) {
        fprintf(stderr, "Isonic: offscreen AA target failed (%s), rendering without supersampling.\n", SDL_GetError());
    }

    while (app.running) {
        Uint32 frame_start = SDL_GetTicks();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            app_handle_event(&app, &event);
        }

        app_update(&app);

        if (scene_target != NULL && (app.window_w != scene_w || app.window_h != scene_h)) {
            SDL_DestroyTexture(scene_target);
            scene_target = create_scene_target(renderer, app.window_w, app.window_h);
            scene_w = app.window_w;
            scene_h = app.window_h;
        }

        if (scene_target != NULL) {
            SDL_SetRenderTarget(renderer, scene_target);
            SDL_RenderSetLogicalSize(renderer, app.window_w, app.window_h);
            app_render(&app, renderer);

            SDL_SetRenderTarget(renderer, NULL);
            SDL_RenderSetLogicalSize(renderer, 0, 0);
            SDL_RenderCopy(renderer, scene_target, NULL, NULL);
        } else {
            app_render(&app, renderer);
        }
        SDL_RenderPresent(renderer);

        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < TARGET_FRAME_MS) {
            SDL_Delay(TARGET_FRAME_MS - elapsed);
        }
    }

    if (scene_target != NULL) SDL_DestroyTexture(scene_target);
    app_shutdown(&app);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
