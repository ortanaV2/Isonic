#ifndef ISONIC_CAMERA_H
#define ISONIC_CAMERA_H

#define BASE_CELL_PX 20.0f
#define CAMERA_MIN_ZOOM 0.25f
#define CAMERA_MAX_ZOOM 4.0f

typedef struct {
    float zoom;    /* multiplier applied to BASE_CELL_PX */
    float pan_x, pan_y; /* screen-space pixel offset of grid origin (0,0) */
} Camera;

void camera_init(Camera *cam);
float camera_cell_px(const Camera *cam);

void camera_grid_to_screen(const Camera *cam, int gx, int gy, int *sx, int *sy);
/* Same as camera_grid_to_screen but for an unrounded float grid position -
   the mirror image of camera_screen_to_grid_f, for re-deriving a screen
   point every frame from a grid position that was fixed once (e.g. a
   marquee-select corner) instead of from a stale screen pixel that a
   mid-drag zoom would silently reinterpret as a different grid point. */
void camera_grid_to_screen_f(const Camera *cam, float gx, float gy, int *sx, int *sy);
/* Rounds to the nearest grid lattice point - for snapping wire endpoints,
   pins, and drag anchors, all of which live at grid vertices. */
void camera_screen_to_grid(const Camera *cam, int sx, int sy, int *gx, int *gy);
/* Unrounded float grid position, for distance-based hit-testing. */
void camera_screen_to_grid_f(const Camera *cam, int sx, int sy, float *gx, float *gy);
/* Floors to the grid cell the point falls inside - unlike camera_screen_to_grid
   (nearest lattice point), this is "which cell is the cursor over", the
   semantics a component's [grid_x, grid_x+w) x [grid_y, grid_y+h) box test
   needs. Using the rounded variant there was systematically off by up to half
   a cell: it made the hitbox bulge outside the drawn body on one edge while
   falling short of it on the opposite edge. */
void camera_screen_to_grid_floor(const Camera *cam, int sx, int sy, int *gx, int *gy);

void camera_pan(Camera *cam, int dx_screen, int dy_screen);
/* delta > 0 zooms in, delta < 0 zooms out, anchored at (cursor_sx, cursor_sy). */
void camera_zoom_at(Camera *cam, int cursor_sx, int cursor_sy, int delta);

#endif
