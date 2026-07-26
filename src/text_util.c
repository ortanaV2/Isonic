#include <stdio.h>
#include <math.h>
#include "text_util.h"

static const char *k_candidate_fonts[] = {
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    NULL
};

/* Only ever two fonts live at once (app->font, app->font_large) for the
   whole process lifetime - see app.c - so a tiny fixed table is plenty. */
#define MAX_TRACKED_FONTS 8
static TTF_Font *g_tracked_fonts[MAX_TRACKED_FONTS];
static float g_tracked_scales[MAX_TRACKED_FONTS];
static int g_tracked_count = 0;

static float font_display_scale(TTF_Font *font) {
    for (int i = 0; i < g_tracked_count; i++) {
        if (g_tracked_fonts[i] == font) return g_tracked_scales[i];
    }
    return 1.0f;
}

static void track_font_scale(TTF_Font *font, float display_scale) {
    if (font == NULL || g_tracked_count >= MAX_TRACKED_FONTS) return;
    g_tracked_fonts[g_tracked_count] = font;
    g_tracked_scales[g_tracked_count] = display_scale;
    g_tracked_count++;
}

TTF_Font *text_util_load_font(int point_size, float display_scale) {
    if (display_scale <= 0.0f) display_scale = 1.0f;
    int scaled_size = (int)lroundf(point_size * display_scale);
    if (scaled_size < 1) scaled_size = 1;
    for (int i = 0; k_candidate_fonts[i] != NULL; i++) {
        TTF_Font *font = TTF_OpenFont(k_candidate_fonts[i], scaled_size);
        if (font != NULL) {
            track_font_scale(font, display_scale);
            return font;
        }
    }
    fprintf(stderr, "Isonic: could not load any system font, text rendering disabled.\n");
    return NULL;
}

void text_util_draw(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    text_util_draw_scaled(renderer, font, text, x, y, color, 1.0f);
}

void text_util_draw_scaled(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color, float scale) {
    if (font == NULL || text == NULL || text[0] == '\0') return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture != NULL) {
        float draw_scale = scale / font_display_scale(font);
        SDL_Rect dst = { x, y, (int)lroundf(surface->w * draw_scale), (int)lroundf(surface->h * draw_scale) };
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void text_util_draw_scaled_rotated(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                                    int center_x, int center_y, SDL_Color color, float scale, float angle_deg) {
    if (font == NULL || text == NULL || text[0] == '\0') return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture != NULL) {
        float draw_scale = scale / font_display_scale(font);
        int w = (int)lroundf(surface->w * draw_scale);
        int h = (int)lroundf(surface->h * draw_scale);
        SDL_Rect dst = { center_x - w / 2, center_y - h / 2, w, h };
        SDL_Point center = { w / 2, h / 2 };
        SDL_RenderCopyEx(renderer, texture, NULL, &dst, angle_deg, &center, SDL_FLIP_NONE);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void text_util_measure(TTF_Font *font, const char *text, int *out_w, int *out_h) {
    if (font == NULL || text == NULL) {
        *out_w = 0;
        *out_h = 0;
        return;
    }
    int w, h;
    TTF_SizeUTF8(font, text, &w, &h);
    float s = font_display_scale(font);
    *out_w = (int)lroundf(w / s);
    *out_h = (int)lroundf(h / s);
}

void text_util_font_metrics(TTF_Font *font, int *out_ascent, int *out_descent) {
    if (font == NULL) {
        *out_ascent = 0;
        *out_descent = 0;
        return;
    }
    float s = font_display_scale(font);
    *out_ascent = (int)lroundf(TTF_FontAscent(font) / s);
    *out_descent = (int)lroundf(TTF_FontDescent(font) / s);
}

void text_util_query_texture_size(TTF_Font *font, SDL_Texture *texture, int *out_w, int *out_h) {
    int w = 0, h = 0;
    if (texture != NULL) SDL_QueryTexture(texture, NULL, NULL, &w, &h);
    float s = font_display_scale(font);
    *out_w = (int)lroundf(w / s);
    *out_h = (int)lroundf(h / s);
}
