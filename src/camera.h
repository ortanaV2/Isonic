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
/* Rounds to the nearest grid cell. */
void camera_screen_to_grid(const Camera *cam, int sx, int sy, int *gx, int *gy);
/* Unrounded float grid position, for distance-based hit-testing. */
void camera_screen_to_grid_f(const Camera *cam, int sx, int sy, float *gx, float *gy);

void camera_pan(Camera *cam, int dx_screen, int dy_screen);
/* delta > 0 zooms in, delta < 0 zooms out, anchored at (cursor_sx, cursor_sy). */
void camera_zoom_at(Camera *cam, int cursor_sx, int cursor_sy, int delta);

#endif
