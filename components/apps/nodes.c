/* Nodes app: list of heard Meshtastic nodes (name, SNR, age), each with a needle
 * pointing at the node once both ends have a position. The needles are
 * heading-relative (IMU compass, GPS course while moving, else north-up), so
 * tick() re-aims the drawn ones as you turn instead of re-rendering the list. */
#include "apps/app_iface.h"
#include "apps/app_manager.h"
#include "ui/frame.h"
#include "ui/theme.h"
#include "ui/colors.h"
#include "ui/menubar.h"
#include "net/backend.h"
#include "net/message.h"
#include "services/services.h"
#include "services/compass.h"
#include "drivers/gps.h"
#include "util/geo.h"
#include "util/compass.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "core/nodedb.h"

#define FPI 3.14159265358979323846
#define REAIM_DEG 4.0   /* heading change worth repainting the needles for */

static lv_obj_t *s_list;
static lv_group_t *s_group;
static int s_rendered = -1;
/* Persistent point storage for the per-row bearing needles (lv_line keeps the
 * pointer, it does not copy). Re-rendered rows reuse the same slots. */
static lv_point_precise_t s_arrow_pts[NODEDB_CAP][2];
/* The drawn needles, so a turn can re-aim them without rebuilding the rows.
 * NULL for a row that has none; every entry is stale after lv_obj_clean. */
static lv_obj_t *s_arrow[NODEDB_CAP];
static double s_arrow_brg[NODEDB_CAP];   /* the bearing each row's text shows */
static double s_up;                      /* heading the drawn needles are aimed against */

static uint32_t focused_num(void)
{
    lv_obj_t *f = lv_group_get_focused(s_group);
    return f ? (uint32_t)(uintptr_t)lv_obj_get_user_data(f) : 0;
}

static void message_focused(void)
{
    uint32_t num = focused_num();
    if (!num) return;
    messages_set_target(num);
    app_manager_launch_app(app_messages());
}

static void track_focused(void)
{
    uint32_t num = focused_num();
    if (!num) return;
    tracker_set_target(num);
    app_manager_launch_app(app_tracker());
}

static void node_key(lv_event_t *e)
{
    uint32_t k = lv_event_get_key(e);
    if (k == LV_KEY_DOWN) lv_group_focus_next(s_group);
    else if (k == LV_KEY_UP) lv_group_focus_prev(s_group);
    /* ENTER activates via node_click (LV_EVENT_CLICKED on the key release), so
     * the launch cannot leak the release into the next screen. */
    lv_obj_t *f = lv_group_get_focused(s_group);
    if (f) lv_obj_scroll_to_view(f, LV_ANIM_ON);
}
static void node_click(lv_event_t *e) { (void)e; message_focused(); }

/* Where the top of every row needle points. The rows carry no north-up toggle of
 * their own, so they take the best heading going: compass, GPS course while
 * moving, north-up (up = 0) with neither. */
static compass_src_t row_up(const gps_fix_t *fix, double *up)
{
    compass_reading_t c;
    bool ok = compass_get(&c);
    return compass_pick_up(true, ok, c.heading_deg,
                           fix->has_course, fix->speed, fix->course, up);
}

/* Aim one drawn needle `rel` degrees clockwise from the top of its 26px box. */
static void aim_needle(int i, double rel)
{
    double th = rel * FPI / 180.0;
    s_arrow_pts[i][0].x = 13;            s_arrow_pts[i][0].y = 13;
    s_arrow_pts[i][1].x = 13.0 + 11.0 * sin(th);
    s_arrow_pts[i][1].y = 13.0 - 11.0 * cos(th);
    lv_line_set_points(s_arrow[i], s_arrow_pts[i], 2);   /* also invalidates */
}

static void render(void)
{
    nodedb_t *db = net_nodedb();
    uint32_t keep = focused_num();   /* keep focus/scroll across re-renders */
    lv_obj_clean(s_list);
    memset(s_arrow, 0, sizeof s_arrow);   /* the clean above deleted them all */
    uint32_t now = (uint32_t)time(NULL);
    gps_fix_t fix;
    bool havegps = gps_get_fix(&fix) && fix.valid;
    s_up = 0.0;
    if (havegps) row_up(&fix, &s_up);

    if (db->count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "No nodes heard yet");
        lv_obj_set_style_text_color(empty, theme_hex(C_TEXT_DIM), 0);
        s_rendered = 0;
        return;
    }
    for (int i = 0; i < db->count; i++) {
        node_record_t *r = &db->nodes[i];
        char label[64], line1[110], line2[96], bat[12] = "";
        net_node_label(r->num, r->long_name, r->short_name, label, sizeof label);
        long age = now ? (long)(now - r->last_heard) : 0;
        if (r->has_telemetry && r->battery) snprintf(bat, sizeof bat, "   %d%%", r->battery);
        snprintf(line1, sizeof line1, "%s   SNR %.0f   %lds%s", label, (double)r->snr, age, bat);

        /* Position line: reported coordinates (shown whenever the node has a
         * position), plus distance and bearing from here when we have our own
         * fix. The needle (right) points the same bearing graphically. */
        line2[0] = 0;
        bool show_arrow = false;
        double brg = 0, rel = 0;
        if (r->has_position) {
            double nlat = r->lat_i / 1e7, nlon = r->lon_i / 1e7;
            char nav[44] = "";
            if (havegps) {
                double dist = geo_distance_m(fix.lat, fix.lon, nlat, nlon);
                brg = geo_bearing_deg(fix.lat, fix.lon, nlat, nlon);
                char ds[16];
                if (dist >= 1000.0) snprintf(ds, sizeof ds, "%.1fkm", dist / 1000.0);
                else snprintf(ds, sizeof ds, "%.0fm", dist);
                snprintf(nav, sizeof nav, "   %s  %.0f %s", ds, brg,
                         compass_cardinal(brg));
                /* The printed bearing stays true north; the needle is turned into
                 * the current heading, so it is a direction to walk. */
                rel = compass_wrap360(brg - s_up);
                show_arrow = true;
            }
            snprintf(line2, sizeof line2, LV_SYMBOL_GPS " %.5f, %.5f%s", nlat, nlon, nav);
        }

        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_add_style(btn, &st_card, 0);
        lv_obj_add_style(btn, &st_card_sel, LV_STATE_FOCUSED);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)r->num);
        lv_obj_add_event_cb(btn, node_key, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(btn, node_click, LV_EVENT_CLICKED, NULL);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *col = lv_obj_create(btn);                 /* text column */
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 1, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *l = lv_label_create(col);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_text(l, line1);
        if (line2[0]) {
            lv_obj_t *l2 = lv_label_create(col);
            lv_label_set_long_mode(l2, LV_LABEL_LONG_DOT);
            lv_obj_set_width(l2, LV_PCT(100));
            lv_obj_set_style_text_color(l2, theme_hex(C_TEXT_DIM), 0);
            lv_label_set_text(l2, line2);
        }

        if (show_arrow && i < NODEDB_CAP) {                 /* bearing needle */
            lv_obj_t *box = lv_obj_create(btn);
            lv_obj_set_size(box, 26, 26);
            lv_obj_set_style_bg_color(box, theme_hex(C_SURFACE_2), 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(box, 0, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *needle = lv_line_create(box);
            lv_obj_set_style_line_width(needle, 3, 0);
            lv_obj_set_style_line_color(needle, theme_hex(C_ACCENT), 0);
            lv_obj_set_style_line_rounded(needle, true, 0);
            s_arrow[i] = needle;
            s_arrow_brg[i] = brg;
            aim_needle(i, rel);
        }

        if (s_group) lv_group_add_obj(s_group, btn);
    }
    if (keep) {
        uint32_t cnt = lv_obj_get_child_count(s_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *btn = lv_obj_get_child(s_list, (int32_t)i);
            if ((uint32_t)(uintptr_t)lv_obj_get_user_data(btn) == keep) {
                lv_group_focus_obj(btn);
                lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
                break;
            }
        }
    }
    s_rendered = db->count;
}

static void build(lv_obj_t **screen, lv_group_t *group)
{
    static frame_t f;
    frame_create(&f, "Nodes");
    s_group = group;
    s_list = lv_obj_create(f.body);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 3, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    s_rendered = -1;
    render();
    *screen = f.screen;
    menubar_set_labels("Message", "Track", "Announ.", "", "");
}

static void tick(void)
{
    if (net_peer_count() != s_rendered) { render(); return; }

    /* Turning in place invalidates every needle while the node count -- the only
     * thing that used to trigger a render -- stands still. A render() would fix
     * them by rebuilding the rows, which costs the focus and the scroll position,
     * so the drawn needles are re-aimed in place instead: no objects created or
     * destroyed, nothing touched but the line points. The gate keeps compass
     * jitter from repainting the list at the 10 Hz tick rate. */
    gps_fix_t fix;
    if (!gps_get_fix(&fix) || !fix.valid) return;   /* no fix, so no needles exist */
    double up = 0.0;
    row_up(&fix, &up);
    if (compass_ang_diff(up, s_up) < REAIM_DEG) return;
    s_up = up;
    for (int i = 0; i < NODEDB_CAP; i++)
        if (s_arrow[i]) aim_needle(i, compass_wrap360(s_arrow_brg[i] - up));
}

static void on_fkey(int n)
{
    if (n == 1) message_focused();
    else if (n == 2) track_focused();
    else if (n == 3) mesh_svc_announce_now();   /* tell the mesh we're here now */
}

const app_def_t *app_nodes(void)
{
    static const app_def_t def = {
        .name = "Nodes", .icon = LV_SYMBOL_LIST,
        .build = build, .tick = tick, .on_fkey = on_fkey,
    };
    return &def;
}
