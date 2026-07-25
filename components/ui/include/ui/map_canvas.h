/* Shared offline-map canvas: streams the /spiffs/map.vmap vector map and draws
 * roads/water, plus caller-supplied overlays (a trail polyline and point
 * markers), all through one projection so they stay aligned. It is a pure
 * renderer -- it knows nothing about GPS, the track service or the node DB; the
 * app gathers that data and feeds it in.
 *
 * Generalizes what used to live inside the radar app (radar.c map_redraw): the
 * radar scope uses MAP_CLIP_CIRCLE, the full-screen Map and Breadcrumbs views
 * use MAP_CLIP_RECT. The projection math is util/map_proj; the file reader is
 * util/vmap.
 *
 * Lifecycle: create once, then each refresh set the view, and if the view moved
 * enough (map_canvas_view_dirty) repaint between begin/end:
 *
 *     map_canvas_set_view(mc, lat, lon, up, range);
 *     if (map_canvas_view_dirty(mc)) {
 *         map_canvas_begin(mc);
 *         map_canvas_draw_basemap(mc);
 *         map_canvas_draw_polyline(mc, trail, n, C_ACCENT_DK);
 *         map_canvas_draw_marker(mc, lat, lon, C_ACCENT, 6);
 *         map_canvas_end(mc);
 *     }
 */
#ifndef UI_MAP_CANVAS_H
#define UI_MAP_CANVAS_H

#include "lvgl.h"
#include "util/geo.h"

typedef enum {
    MAP_CLIP_CIRCLE,   /* inscribed circle of radius radius_px (radar scope) */
    MAP_CLIP_RECT,     /* the full w x h canvas (full-screen views) */
} map_clip_t;

typedef struct map_canvas map_canvas_t;

/* Create a w x h RGB565 canvas (PSRAM-backed, falling back to internal RAM) as a
 * child of `parent`. radius_px is the projection scale: a feature range_m away
 * lands radius_px from the centre. For a round scope pass the rim radius (inset
 * inside the canvas, e.g. 42 in a 92 px canvas); for a rectangular view pass
 * min(w,h)/2. Returns NULL only if the struct allocation fails; a failed canvas
 * buffer still returns a usable (no-op drawing) handle. */
map_canvas_t *map_canvas_create(lv_obj_t *parent, int w, int h,
                                map_clip_t clip, float radius_px);

/* The underlying LVGL canvas object, for positioning / show / hide. */
lv_obj_t *map_canvas_obj(map_canvas_t *mc);

/* Set the view centre (decimal degrees), up direction (true degrees, 0 = north)
 * and the distance radius_px represents (metres, > 0). */
void map_canvas_set_view(map_canvas_t *mc, double center_lat, double center_lon,
                         double up_deg, double range_m);

/* True if the view set since the last repaint moved/rotated/zoomed enough to be
 * worth redrawing (and the minimum redraw interval has passed). Lets callers
 * skip the costly repaint on ticks where nothing changed. */
bool map_canvas_view_dirty(map_canvas_t *mc);

/* Force the next map_canvas_view_dirty to return true (e.g. after toggling the
 * view on, or losing/regaining a GPS fix). */
void map_canvas_invalidate(map_canvas_t *mc);

/* Begin a repaint: clear the canvas and open the draw layer. Pair with
 * map_canvas_end. The draw_* calls below are valid only between the two. */
void map_canvas_begin(map_canvas_t *mc);

/* Stream /spiffs/map.vmap and draw roads (dim) and water (cyan) for the current
 * view, culling out-of-view features by bounding box. */
void map_canvas_draw_basemap(map_canvas_t *mc);

/* Draw a polyline through `n` geographic points in the same projection (e.g. a
 * recorded trail). `color` is a C_* token from ui/colors.h. */
void map_canvas_draw_polyline(map_canvas_t *mc, const geo_pt_t *pts, int n, long color);

/* Draw a filled round marker `size` px across at a geographic point, clipped to
 * the viewport. `color` is a C_* token. */
void map_canvas_draw_marker(map_canvas_t *mc, double lat, double lon, long color, int size);

/* Finish the repaint (flush the draw layer) and stamp the drawn view so
 * map_canvas_view_dirty can compare against it next time. */
void map_canvas_end(map_canvas_t *mc);

/* Whether a readable /spiffs/map.vmap was found during the last
 * map_canvas_draw_basemap (so callers can show a "no map" hint). */
bool map_canvas_present(map_canvas_t *mc);

/* Free the canvas buffer and the handle. Call from the app's close(). */
void map_canvas_delete(map_canvas_t *mc);

#endif /* UI_MAP_CANVAS_H */
