#ifndef ISONIC_TEXT_UTIL_H
#define ISONIC_TEXT_UTIL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Tries a handful of common Windows system font paths, opening the font at
   `point_size * display_scale` points. `display_scale` is remembered against
   the returned TTF_Font* so every later text_util_measure/draw/query call
   for it automatically renders/reports sizes back down by that same factor -
   the same oversample-then-shrink trick render.c already used for crisp
   canvas labels (see LABEL_FONT_POINT_SIZE), just generalized so a per-
   monitor DPI scale doesn't need to be threaded through every panel's layout
   math by hand. Pass 1.0f for a font that should NOT get this treatment
   (e.g. font_large, whose oversampling is already its own fixed, zoom-driven
   ratio unrelated to DPI). Returns NULL if none could be loaded (callers must
   tolerate a NULL font by simply skipping text). */
TTF_Font *text_util_load_font(int point_size, float display_scale);

/* No-op if font is NULL. Draws left-aligned text with (x,y) as the top-left corner. */
void text_util_draw(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color);

/* Same as text_util_draw, but blits the glyph texture scaled by `scale` (1.0 =
   native size) so labels visually track the current zoom level. */
void text_util_draw_scaled(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color, float scale);

/* Same as text_util_draw_scaled, but centered on (center_x, center_y) and
   rotated clockwise by angle_deg around that center - used for the IC body's
   large name label, which runs along the body's long axis. */
void text_util_draw_scaled_rotated(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                                    int center_x, int center_y, SDL_Color color, float scale, float angle_deg);

/* Returns the rendered pixel width/height of text, or 0/0 if font is NULL. */
void text_util_measure(TTF_Font *font, const char *text, int *out_w, int *out_h);

/* Baseline-to-top (ascent) and baseline-to-bottom (descent, negative per
   SDL_ttf's own convention) font metrics, 0/0 if font is NULL. text_util_
   measure's height is the full ascent-to-descent line box - most ordinary
   text (no descenders like g/y/p/q/j) only actually draws ink in the
   ascent portion, so centering on the full box (as if descent space held
   visible content too) reads as sitting a bit low. Callers that need the
   text's actual visual center - not just its bounding box's geometric one -
   use this to correct for that gap (see render.c's text_label_vcenter_offset). */
void text_util_font_metrics(TTF_Font *font, int *out_ascent, int *out_descent);

/* SDL_QueryTexture, but for a texture built from `font`'s glyphs (e.g.
   taskbar.c's own cached label textures, built via TTF_RenderUTF8_Blended
   directly instead of through text_util_draw) - divides the queried size by
   the same display_scale text_util_load_font recorded for `font`, so a
   texture built from an oversampled font still reports/blits at its intended
   logical footprint. */
void text_util_query_texture_size(TTF_Font *font, SDL_Texture *texture, int *out_w, int *out_h);

#endif
