/* Map app: a full-screen offline map. You-centered by default; in heading-up
 * mode the view turns with the tilt-compensated compass, falls back to GPS
 * course over ground while moving, and stays north-up when neither is usable
 * (view_up() shares that arbitration with radar's scope_up through
 * compass_pick_up). F3 unlocks free panning with the arrow keys; F3 again
 * re-centers on you. Roads/water come from the offline /spiffs/map.vmap; your
 * recorded trail and positioned mesh nodes are drawn on top. All rendering is
 * the shared ui/map_canvas widget. */
#include "apps/app_iface.h"
#include "ui/frame.h"
#include "ui/theme.h"
#include "ui/colors.h"
#include "ui/menubar.h"
#include "ui/layout.h"
#include "ui/map_canvas.h"
#include "net/backend.h"
#include "core/nodedb.h"
#include "drivers/gps.h"
#include "services/compass.h"
#include "services/track.h"
#include "util/compass.h"
#include "util/vmap.h"

#include <math.h>
#include <stdio.h>

#define DEG2RAD     0.017453292519943295
#define TRAIL_MAX   512

/* Zoom steps: metres represented by the centre-to-nearest-edge distance. */
static const double RANGES[] = {100.0, 200.0, 500.0, 1000.0, 2000.0};
#define NRANGES ((int)(sizeof RANGES / sizeof RANGES[0]))

static map_canvas_t *s_mc;
static lv_obj_t *s_info;
static int s_mode;              /* 0 = north-up, 1 = heading-up (compass or course) */
static int s_range_idx = 2;     /* default 500 m */
static bool s_pan;              /* free-pan mode */
static bool s_pan_seeded;       /* s_pan_lat/lon hold a meaningful point */
static double s_pan_lat, s_pan_lon;
static bool s_shown;            /* the canvas currently shows a drawn view */
static int s_last_tn, s_last_nodes;

static void set_labels(void)
{
    menubar_set_labels(s_mode ? "North up" : "Head up", "Range",
                       s_pan ? "Center" : "Pan", "", "");
}

static void fmt_dist(double m, char *out, int cap)
{
    if (m >= 1000.0) snprintf(out, cap, "%.1f km", m / 1000.0);
    else snprintf(out, cap, "%.0f m", m);
}

/* View up in true degrees: the compass first, then GPS course while moving,
 * north-up otherwise. Returns which source won. */
static compass_src_t view_up(const gps_fix_t *fix, double *up)
{
    compass_reading_t c;
    bool ok = compass_get(&c);
    return compass_pick_up(s_mode == 1, ok, c.heading_deg,
                           fix->has_course, fix->speed, fix->course, up);
}

/* Centre of the loaded map file, to seed pan mode when there is no GPS fix. */
static bool map_file_center(double *lat, double *lon)
{
    vmap_reader_t r;
    if (vmap_open(&r, VMAP_DEFAULT_PATH) != 0) return false;
    *lat = (r.hdr.bbox.min_lat_e7 + (double)r.hdr.bbox.max_lat_e7) / 2.0 / 1e7;
    *lon = (r.hdr.bbox.min_lon_e7 + (double)r.hdr.bbox.max_lon_e7) / 2.0 / 1e7;
    vmap_close(&r);
    return true;
}

static void pan_by(int dlat_sign, int dlon_sign)
{
    double step_m = RANGES[s_range_idx] * 0.5;     /* half a screen-radius / press */
    double coslat = cos(s_pan_lat * DEG2RAD);
    if (fabs(coslat) < 1e-6) coslat = 1e-6;
    s_pan_lat += dlat_sign * step_m / 111320.0;
    s_pan_lon += dlon_sign * step_m / (111320.0 * coslat);
}

static void map_key_cb(lv_event_t *e)
{
    if (!s_pan) return;
    uint32_t k = lv_event_get_key(e);
    if (k == LV_KEY_UP)         pan_by(+1, 0);
    else if (k == LV_KEY_DOWN)  pan_by(-1, 0);
    else if (k == LV_KEY_RIGHT) pan_by(0, +1);
    else if (k == LV_KEY_LEFT)  pan_by(0, -1);
    else return;
    map_canvas_invalidate(s_mc);   /* repaint on the next tick */
}

static void build(lv_obj_t **screen, lv_group_t *group)
{
    static frame_t f;
    frame_create(&f, "Map");
    lv_obj_remove_flag(f.body, LV_OBJ_FLAG_SCROLLABLE);

    int w = CONTENT_W - 8;          /* body pad is 4 each side */
    int h = BODY_H - 8;
    s_mc = map_canvas_create(f.body, w, h, MAP_CLIP_RECT, (w < h ? w : h) / 2.0f);
    lv_obj_t *cv = map_canvas_obj(s_mc);
    lv_obj_set_pos(cv, 0, 0);
    lv_obj_set_style_outline_width(cv, 0, LV_STATE_FOCUSED);
    if (group) {
        lv_obj_add_event_cb(cv, map_key_cb, LV_EVENT_KEY, NULL);
        lv_group_add_obj(group, cv);
        lv_group_focus_obj(cv);
    }

    s_info = lv_label_create(f.body);
    lv_obj_set_style_bg_color(s_info, theme_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_info, 4, 0);
    lv_obj_set_style_pad_ver(s_info, 1, 0);
    lv_obj_set_style_text_color(s_info, theme_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(s_info, theme_font_body(), 0);
    lv_obj_align(s_info, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(s_info, "");

    s_pan = false;
    s_pan_seeded = false;
    s_shown = false;
    s_last_tn = -1;
    s_last_nodes = -1;
    *screen = f.screen;
    set_labels();
}

static void on_fkey(int n)
{
    if (n == 1) {
        s_mode = !s_mode;
        map_canvas_invalidate(s_mc);
    } else if (n == 2) {
        s_range_idx = (s_range_idx + 1) % NRANGES;
        map_canvas_invalidate(s_mc);
    } else if (n == 3) {
        s_pan = !s_pan;
        if (s_pan) {
            gps_fix_t fix;
            if (gps_get_fix(&fix) && fix.valid) {
                s_pan_lat = fix.lat; s_pan_lon = fix.lon; s_pan_seeded = true;
            } else if (!s_pan_seeded) {
                if (map_file_center(&s_pan_lat, &s_pan_lon)) s_pan_seeded = true;
            }
        }
        map_canvas_invalidate(s_mc);
    }
    set_labels();
}

static void tick(void)
{
    gps_fix_t fix;
    bool havefix = gps_get_fix(&fix) && fix.valid;
    double range = RANGES[s_range_idx];

    if (!s_pan && !havefix) {
        lv_label_set_text(s_info, "No GPS fix. Press F3 to pan the map.");
        if (s_shown) { map_canvas_begin(s_mc); map_canvas_end(s_mc); s_shown = false; }
        return;
    }

    /* Gather overlays first, so we can repaint when they change even if the
     * view is static (e.g. a new node, or a trail point while stopped). */
    static geo_pt_t trail[TRAIL_MAX];
    int tn = track_get_points(trail, TRAIL_MAX);
    nodedb_t *db = net_nodedb();
    int nn = 0;
    for (int i = 0; i < db->count; i++) if (db->nodes[i].has_position) nn++;
    if (tn != s_last_tn || nn != s_last_nodes) {
        map_canvas_invalidate(s_mc);
        s_last_tn = tn; s_last_nodes = nn;
    }

    double clat, clon, up;
    compass_src_t src = COMPASS_SRC_NONE;
    /* Panning stays north-up on purpose: a view that turns with your heading
     * while the arrows walk the centre away from you is impossible to steer. */
    if (s_pan) { clat = s_pan_lat; clon = s_pan_lon; up = 0.0; }
    else { clat = fix.lat; clon = fix.lon; src = view_up(&fix, &up); }

    map_canvas_set_view(s_mc, clat, clon, up, range);
    if (map_canvas_view_dirty(s_mc)) {
        map_canvas_begin(s_mc);
        map_canvas_draw_basemap(s_mc);
        map_canvas_draw_polyline(s_mc, trail, tn, C_ACCENT_DK);
        for (int i = 0; i < db->count; i++) {
            node_record_t *r = &db->nodes[i];
            if (r->has_position)
                map_canvas_draw_marker(s_mc, r->lat_i / 1e7, r->lon_i / 1e7, C_TEXT, 5);
        }
        if (havefix)
            map_canvas_draw_marker(s_mc, fix.lat, fix.lon, C_ACCENT, 7);
        map_canvas_end(s_mc);
        s_shown = true;
    }

    char rs[16];
    fmt_dist(range, rs, sizeof rs);
    /* The wording is util/compass's job so every heading-up view names the same
     * fallback the same way: "move" is only honest advice when there is no
     * compass to wait for, and a stored-but-disabled calibration must not send
     * the user off to sweep what is already swept. */
    compass_status_t st;
    compass_get_status(&st);
    compass_hint_t hint = compass_hint(src, st.state, st.cal_stored, st.cal_in_use);
    char info[96];
    const char *m = s_pan ? "Pan" : compass_up_label(s_mode == 1, src, hint);
    snprintf(info, sizeof info, "%s  %s%s", m, rs,
             map_canvas_present(s_mc) ? "" : "  (no map)");
    lv_label_set_text(s_info, info);
}

static void close_app(void)
{
    if (s_mc) { map_canvas_delete(s_mc); s_mc = NULL; }
    s_info = NULL;
}

const app_def_t *app_map(void)
{
    static const app_def_t def = {
        .name = "Map", .icon = LV_SYMBOL_IMAGE,
        .build = build, .on_fkey = on_fkey, .tick = tick, .close = close_app,
    };
    return &def;
}
