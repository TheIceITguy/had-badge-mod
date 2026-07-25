/* Breadcrumbs app: start and stop GPS track recording, with an optional map view
 * (F2) that draws the recorded trail and positioned nodes over the offline map. */
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
#include "services/track.h"

#include <stdio.h>

#define TRAIL_MAX        512
#define TRACK_MAP_RANGE  500.0   /* metres centre-to-edge for the trail map */

static lv_obj_t *s_status, *s_file, *s_hint;
static map_canvas_t *s_mc;
static bool s_map_on;            /* map view instead of the text view */
static bool s_shown;             /* the canvas currently shows a drawn view */
static int s_last_tn, s_last_nodes;

static void refresh_menubar(void)
{
    menubar_set_labels(track_is_active() ? "Stop" : "Start",
                       s_map_on ? "Map*" : "Map", "", "", "Back");
}

/* Swap between the text view and the map view. */
static void set_view_mode(void)
{
    lv_obj_t *cv = map_canvas_obj(s_mc);
    if (s_map_on) {
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_file, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cv, LV_OBJ_FLAG_HIDDEN);
        map_canvas_invalidate(s_mc);
        s_last_tn = -1; s_last_nodes = -1;
    } else {
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_file, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build(lv_obj_t **screen, lv_group_t *group)
{
    (void)group;
    static frame_t f;
    frame_create(&f, "Breadcrumbs");
    lv_obj_set_flex_flow(f.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.body, 4, 0);

    s_status = lv_label_create(f.body);
    lv_obj_set_style_text_font(s_status, theme_font_title(), 0);
    lv_label_set_text(s_status, "Idle");

    s_file = lv_label_create(f.body);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_file, LV_PCT(100));
    lv_label_set_text(s_file, "");

    s_hint = lv_label_create(f.body);
    lv_obj_set_style_text_color(s_hint, theme_hex(C_TEXT_DIM), 0);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_hint, "F1 records. F2 shows the trail on the map. A red dot in the sidebar means a track is recording.");

    /* Map view, overlaid on the body and hidden until F2 toggles it on. */
    int w = CONTENT_W - 8, h = BODY_H - 8;   /* body pad is 4 each side */
    s_mc = map_canvas_create(f.body, w, h, MAP_CLIP_RECT, (w < h ? w : h) / 2.0f);
    lv_obj_t *cv = map_canvas_obj(s_mc);
    lv_obj_add_flag(cv, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(cv, 0, 0);
    lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);

    s_map_on = false;
    s_shown = false;
    s_last_tn = -1;
    s_last_nodes = -1;
    *screen = f.screen;
    refresh_menubar();
}

static void on_fkey(int n)
{
    if (n == 1) {
        if (track_is_active()) {
            track_stop();
        } else if (!track_start()) {
            lv_label_set_text(s_hint, "Cannot start: the clock is not set. Enable GPS and wait for a fix first.");
        }
        refresh_menubar();
    } else if (n == 2) {
        s_map_on = !s_map_on;
        set_view_mode();
        refresh_menubar();
    }
}

static void tick(void)
{
    char b[64];
    if (track_is_active()) {
        snprintf(b, sizeof b, "Recording  %d pts", track_point_count());
        lv_label_set_text(s_status, b);
        lv_obj_set_style_text_color(s_status, theme_hex(C_CRIT), 0);
        snprintf(b, sizeof b, "File: %s", track_filename());
        lv_label_set_text(s_file, b);
    } else {
        lv_label_set_text(s_status, "Idle");
        lv_obj_set_style_text_color(s_status, theme_hex(C_TEXT), 0);
        if (track_filename()[0]) {
            snprintf(b, sizeof b, "Last: %s", track_filename());
            lv_label_set_text(s_file, b);
        }
    }

    if (!s_map_on) return;

    /* Map view: centre on the current fix, else the last recorded point. */
    static geo_pt_t trail[TRAIL_MAX];
    int tn = track_get_points(trail, TRAIL_MAX);
    gps_fix_t fix;
    bool havefix = gps_get_fix(&fix) && fix.valid;
    double clat, clon;
    if (havefix) { clat = fix.lat; clon = fix.lon; }
    else if (tn > 0) { clat = trail[tn - 1].lat; clon = trail[tn - 1].lon; }
    else {
        if (s_shown) { map_canvas_begin(s_mc); map_canvas_end(s_mc); s_shown = false; }
        return;
    }

    nodedb_t *db = net_nodedb();
    int nn = 0;
    for (int i = 0; i < db->count; i++) if (db->nodes[i].has_position) nn++;
    if (tn != s_last_tn || nn != s_last_nodes) {
        map_canvas_invalidate(s_mc);
        s_last_tn = tn; s_last_nodes = nn;
    }

    map_canvas_set_view(s_mc, clat, clon, 0.0, TRACK_MAP_RANGE);
    if (map_canvas_view_dirty(s_mc)) {
        map_canvas_begin(s_mc);
        map_canvas_draw_basemap(s_mc);
        map_canvas_draw_polyline(s_mc, trail, tn, C_ACCENT);
        for (int i = 0; i < db->count; i++) {
            node_record_t *r = &db->nodes[i];
            if (r->has_position)
                map_canvas_draw_marker(s_mc, r->lat_i / 1e7, r->lon_i / 1e7, C_TEXT, 5);
        }
        if (havefix) map_canvas_draw_marker(s_mc, fix.lat, fix.lon, C_ACCENT, 7);
        map_canvas_end(s_mc);
        s_shown = true;
    }
}

static void close_app(void)
{
    if (s_mc) { map_canvas_delete(s_mc); s_mc = NULL; }
}

const app_def_t *app_track(void)
{
    static const app_def_t def = {
        .name = "Bread-crumbs", .icon = LV_SYMBOL_SAVE,   /* hyphen lets the tile wrap to 2 lines */
        .build = build, .on_fkey = on_fkey, .tick = tick, .close = close_app,
    };
    return &def;
}
