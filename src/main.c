#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "app.h"
#include "ic_registry.h"
#include "ics/sn7408.h"

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
        "Isonic",
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

    ic_sn7408_register();

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
