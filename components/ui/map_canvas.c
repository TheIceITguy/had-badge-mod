/* See ui/map_canvas.h. The basemap pass is the radar overlay generalized: open
 * /spiffs/map.vmap, cull out-of-view features by bbox, project each kept
 * polyline and clip it to the viewport (circle for the scope, rectangle for the
 * full-screen views). Overlays (trail, markers) go through the same projection
 * so they stay registered with the roads. */
#include "ui/map_canvas.h"
#include "ui/theme.h"
#include "ui/colors.h"
#include "util/vmap.h"
#include "util/map_proj.h"
#include "util/geo.h"

#include "esp_heap_caps.h"
#include <stdlib.h>
#include <math.h>

#define MAP_CANVAS_MARGIN  1.15     /* enlarge the cull box so rim-crossers survive */
#define MAP_CANVAS_MIN_MS  500      /* min interval between repaints (ms) */
#define MAP_CANVAS_UP_EPS  3.0      /* heading change that forces a repaint (deg) */

struct map_canvas {
    lv_obj_t  *canvas;
    void      *buf;
    int        w, h;
    map_clip_t clip;
    float      cx, cy, radius_px, max_vis_px;

    /* current view (map_canvas_set_view) */
    double     center_lat, center_lon, up_deg, range_m;
    bool       present;             /* map.vmap readable on the last basemap draw */
    lv_layer_t layer;               /* live between begin/end */

    /* last painted view, for the dirty check */
    bool       drawn_valid;
    double     d_lat, d_lon, d_up, d_range;
    uint32_t   d_ms;
};

static double ang_diff(double a, double b)
{
    double d = fmod(a - b + 540.0, 360.0) - 180.0;
    return d < 0 ? -d : d;
}

static void proj_deg(map_canvas_t *mc, double lat, double lon, float *x, float *y)
{
    map_project(mc->center_lat, mc->center_lon, lat, lon, mc->up_deg, mc->range_m,
                mc->cx, mc->cy, mc->radius_px, x, y);
}

static bool clip_seg(map_canvas_t *mc, float x0, float y0, float x1, float y1,
                     float *vx0, float *vy0, float *vx1, float *vy1)
{
    if (mc->clip == MAP_CLIP_CIRCLE)
        return map_clip_segment_px(x0, y0, x1, y1, mc->cx, mc->cy, mc->radius_px,
                                   vx0, vy0, vx1, vy1);
    return map_clip_segment_rect(x0, y0, x1, y1, 0.0f, 0.0f, (float)mc->w, (float)mc->h,
                                 vx0, vy0, vx1, vy1);
}

static bool in_view(map_canvas_t *mc, float x, float y)
{
    if (mc->clip == MAP_CLIP_CIRCLE) {
        float dx = x - mc->cx, dy = y - mc->cy;
        return dx * dx + dy * dy <= mc->radius_px * mc->radius_px;
    }
    return x >= 0.0f && x <= (float)mc->w && y >= 0.0f && y <= (float)mc->h;
}

static void draw_clipped_line(map_canvas_t *mc, lv_draw_line_dsc_t *dsc,
                              float px, float py, float qx, float qy)
{
    float a, b, c, d;
    if (!clip_seg(mc, px, py, qx, qy, &a, &b, &c, &d)) return;
    dsc->p1.x = (int32_t)lroundf(a); dsc->p1.y = (int32_t)lroundf(b);
    dsc->p2.x = (int32_t)lroundf(c); dsc->p2.y = (int32_t)lroundf(d);
    lv_draw_line(&mc->layer, dsc);
}

map_canvas_t *map_canvas_create(lv_obj_t *parent, int w, int h,
                                map_clip_t clip, float radius_px)
{
    map_canvas_t *mc = calloc(1, sizeof *mc);
    if (!mc) return NULL;
    mc->w = w; mc->h = h; mc->clip = clip; mc->radius_px = radius_px;
    mc->cx = w / 2.0f; mc->cy = h / 2.0f;
    mc->max_vis_px = (clip == MAP_CLIP_CIRCLE)
                   ? radius_px : sqrtf(mc->cx * mc->cx + mc->cy * mc->cy);

    mc->canvas = lv_canvas_create(parent);
    lv_obj_set_size(mc->canvas, w, h);
    lv_obj_set_style_pad_all(mc->canvas, 0, 0);
    lv_obj_set_style_border_width(mc->canvas, 0, 0);
    lv_obj_remove_flag(mc->canvas, LV_OBJ_FLAG_SCROLLABLE);

    size_t bsz = LV_CANVAS_BUF_SIZE(w, h, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    mc->buf = heap_caps_malloc(bsz, MALLOC_CAP_SPIRAM);
    if (!mc->buf) mc->buf = heap_caps_malloc(bsz, MALLOC_CAP_DEFAULT);
    if (mc->buf) {
        lv_canvas_set_buffer(mc->canvas, mc->buf, w, h, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(mc->canvas, theme_hex(C_SURFACE), LV_OPA_COVER);
    }
    return mc;
}

lv_obj_t *map_canvas_obj(map_canvas_t *mc) { return mc ? mc->canvas : NULL; }
bool map_canvas_present(map_canvas_t *mc) { return mc && mc->present; }
void map_canvas_invalidate(map_canvas_t *mc) { if (mc) mc->drawn_valid = false; }

void map_canvas_set_view(map_canvas_t *mc, double center_lat, double center_lon,
                         double up_deg, double range_m)
{
    if (!mc) return;
    mc->center_lat = center_lat;
    mc->center_lon = center_lon;
    mc->up_deg = up_deg;
    mc->range_m = range_m;
}

bool map_canvas_view_dirty(map_canvas_t *mc)
{
    if (!mc || !mc->buf) return false;
    if (!mc->drawn_valid) return true;
    bool need = fabs(mc->range_m - mc->d_range) > mc->d_range * 0.02
             || geo_distance_m(mc->d_lat, mc->d_lon, mc->center_lat, mc->center_lon)
                    > mc->range_m / 40.0
             || ang_diff(mc->up_deg, mc->d_up) > MAP_CANVAS_UP_EPS;
    if (!need) return false;
    return lv_tick_elaps(mc->d_ms) >= MAP_CANVAS_MIN_MS;
}

void map_canvas_begin(map_canvas_t *mc)
{
    if (!mc || !mc->buf) return;
    lv_canvas_fill_bg(mc->canvas, theme_hex(C_SURFACE), LV_OPA_COVER);
    lv_canvas_init_layer(mc->canvas, &mc->layer);
}

void map_canvas_draw_basemap(map_canvas_t *mc)
{
    if (!mc || !mc->buf) return;
    mc->present = false;
    if (mc->range_m <= 0.0) return;

    vmap_reader_t r;
    if (vmap_open(&r, VMAP_DEFAULT_PATH) != 0) return;
    mc->present = true;

    double cull_range = mc->range_m * (mc->max_vis_px / mc->radius_px);
    map_bbox_e7_t view;
    map_view_bbox(mc->center_lat, mc->center_lon, cull_range, MAP_CANVAS_MARGIN, &view);

    lv_draw_line_dsc_t road, water;
    lv_draw_line_dsc_init(&road);
    road.color = theme_hex(C_TEXT_DIM); road.width = 1; road.opa = LV_OPA_COVER;
    lv_draw_line_dsc_init(&water);
    water.color = theme_hex(C_CHARGE); water.width = 1; water.opa = LV_OPA_COVER;

    static int32_t pts[2 * VMAP_MAX_POINTS];   /* static: off the LVGL task stack */
    vmap_feature_t ft;
    while (vmap_next(&r, &ft) == 1) {
        if (!map_bbox_overlap(&view, &ft.bbox)) { vmap_skip_points(&r, &ft); continue; }
        int n = vmap_read_points(&r, &ft, pts, (int)(sizeof pts / sizeof pts[0]));
        if (n < 2) continue;
        lv_draw_line_dsc_t *dsc = (ft.cls == VMAP_CLASS_WATER) ? &water : &road;
        float px, py;
        proj_deg(mc, pts[0] / VMAP_E7, pts[1] / VMAP_E7, &px, &py);
        for (int i = 1; i < n; i++) {
            float qx, qy;
            proj_deg(mc, pts[2 * i] / VMAP_E7, pts[2 * i + 1] / VMAP_E7, &qx, &qy);
            draw_clipped_line(mc, dsc, px, py, qx, qy);
            px = qx; py = qy;
        }
    }
    vmap_close(&r);
}

void map_canvas_draw_polyline(map_canvas_t *mc, const geo_pt_t *pts, int n, long color)
{
    if (!mc || !mc->buf || !pts || n < 2 || mc->range_m <= 0.0) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = theme_hex(color); d.width = 2; d.opa = LV_OPA_COVER;
    float px, py;
    proj_deg(mc, pts[0].lat, pts[0].lon, &px, &py);
    for (int i = 1; i < n; i++) {
        float qx, qy;
        proj_deg(mc, pts[i].lat, pts[i].lon, &qx, &qy);
        draw_clipped_line(mc, &d, px, py, qx, qy);
        px = qx; py = qy;
    }
}

void map_canvas_draw_marker(map_canvas_t *mc, double lat, double lon, long color, int size)
{
    if (!mc || !mc->buf || mc->range_m <= 0.0 || size <= 0) return;
    float x, y;
    proj_deg(mc, lat, lon, &x, &y);
    if (!in_view(mc, x, y)) return;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = theme_hex(color); rd.bg_opa = LV_OPA_COVER; rd.radius = LV_RADIUS_CIRCLE;
    int half = size / 2;
    int32_t x1 = (int32_t)lroundf(x) - half, y1 = (int32_t)lroundf(y) - half;
    lv_area_t a = { x1, y1, x1 + size - 1, y1 + size - 1 };
    lv_draw_rect(&mc->layer, &rd, &a);
}

void map_canvas_end(map_canvas_t *mc)
{
    if (!mc || !mc->buf) return;
    lv_canvas_finish_layer(mc->canvas, &mc->layer);
    mc->d_lat = mc->center_lat; mc->d_lon = mc->center_lon;
    mc->d_up = mc->up_deg;      mc->d_range = mc->range_m;
    mc->d_ms = lv_tick_get();
    mc->drawn_valid = true;
}

void map_canvas_delete(map_canvas_t *mc)
{
    if (!mc) return;
    if (mc->canvas) lv_obj_delete(mc->canvas);
    if (mc->buf) free(mc->buf);
    free(mc);
}
