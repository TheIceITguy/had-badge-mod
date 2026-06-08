"""Central UI theme: color tokens, geometry, fonts, host-testable state->visual
mapping helpers, and lazily-built LVGL style objects.

The constants and mapping helpers import NO lvgl, so they unit-test under CPython.
LVGL style objects are built once via init_styles() at boot (StatusService).
"""

# --- COLORS (RGB hex ints; wrap with lvgl.color_hex at use site) -----------
C_BG        = 0x0E1116   # screen background
C_SURFACE   = 0x171C24   # card / row / bubble (them)
C_SURFACE_2 = 0x222A35   # raised / selected row / input field
C_SIDEBAR   = 0x0A0D11   # sidebar base
C_SIDEBAR_2 = 0x131922   # sidebar gradient top
C_DIVIDER   = 0x2A323D   # hairlines / card borders

C_TEXT      = 0xF2F5F8   # primary text
C_TEXT_DIM  = 0x93A0AE   # secondary (timestamps, sender, hints)
C_TEXT_MUTE = 0x5A6573   # disabled / placeholder

C_ACCENT    = 0xE39810   # primary accent (selection, my-bubble, focus, cursor)
C_ACCENT_DK = 0x8A5C06   # accent gradient bottom / pressed
C_ON_ACCENT = 0x1A1206   # text on accent fills

C_OK     = 0x35C46A      # good
C_WARN   = 0xF2B01E      # warning
C_CRIT   = 0xE5484D      # critical
C_IDLE   = 0x3A4350      # inactive icon element
C_CHARGE = 0x3FC7E0      # charging / USB cyan

# --- SPACING / RADII / BORDERS ---------------------------------------------
PAD_XS, PAD_S, PAD_M, PAD_L = 2, 4, 8, 12
R_CARD, R_BUBBLE, R_PILL = 8, 10, 999
BORDER_HAIR, FOCUS_RING = 1, 2

# --- GEOMETRY (428x142 landscape) ------------------------------------------
SCREEN_W, SCREEN_H = 428, 142
SIDEBAR_W = 28
CONTENT_X = SIDEBAR_W
CONTENT_W = SCREEN_W - SIDEBAR_W      # 400
HEADER_H, FOOTER_H = 22, 22
BODY_H = SCREEN_H - HEADER_H - FOOTER_H   # 98
CONTENT_PAD = PAD_M


# --- FONT ACCESSORS (lazy; pure helpers never call these) ------------------
def f_tiny():
    import lvgl
    return lvgl.font_montserrat_12


def f_body():
    import lvgl
    return lvgl.font_montserrat_14


def f_title():
    import lvgl
    return lvgl.font_montserrat_16


def f_hero():
    import lvgl
    return lvgl.font_montserrat_28


def f_giant():
    import lvgl
    return lvgl.font_montserrat_42


# --- PURE STATE -> VISUAL MAPPERS (host-testable) --------------------------
def battery_state(pct, charging, usb, present):
    """One of: none, charging, usb_full, crit, low, ok."""
    if not present:
        return "none"
    if charging:
        return "charging"
    if usb and pct is not None and pct >= 99:
        return "usb_full"
    if pct is None:
        return "none"
    if pct < 15:
        return "crit"
    if pct < 40:
        return "low"
    return "ok"


def battery_fill_color(pct, charging, present):
    if not present:
        return None
    if charging:
        return C_CHARGE
    if pct is None:
        return None
    if pct < 15:
        return C_CRIT
    if pct < 40:
        return C_WARN
    return C_OK


def battery_fill_units(pct, inner_max=12):
    if pct is None:
        return 0
    return max(1, min(inner_max, round(inner_max * pct / 100.0)))


def wifi_level(state, rssi):
    """0..3 bars lit."""
    if state in ("off", "disabled", None):
        return 0
    if state == "scan":
        return 1
    if state == "ap":
        return 3
    if rssi is None:
        return 1
    if rssi >= -60:
        return 3
    if rssi >= -75:
        return 2
    return 1


def wifi_color(state):
    return {"ap": C_ACCENT, "conn": C_OK, "sta": C_OK, "scan": C_TEXT_DIM}.get(state, C_IDLE)


def mesh_level(backend_up, peers):
    """0..3 dots lit."""
    if not backend_up:
        return 0
    if peers <= 0:
        return 1
    return min(3, 1 + peers)


def gps_state(enabled, fix, sats):
    """off / search / fix2d / fix3d."""
    if not enabled:
        return "off"
    if not fix:
        return "search"
    return "fix3d" if (sats or 0) >= 4 else "fix2d"


# --- LVGL STYLE OBJECTS (built once at boot) -------------------------------
_inited = False
st_screen = st_card = st_card_sel = st_sidebar = st_header = st_footer = None
st_bubble_me = st_bubble_them = st_input = st_hint = st_title = st_value = None


def _mk(**_):
    import lvgl
    s = lvgl.style_t()
    s.init()
    return s


def init_styles():
    """Build and cache the shared styles. Idempotent; call once at boot."""
    global _inited, st_screen, st_card, st_card_sel, st_sidebar, st_header, st_footer
    global st_bubble_me, st_bubble_them, st_input, st_hint, st_title, st_value
    if _inited:
        return
    import lvgl
    hx = lvgl.color_hex

    st_screen = _mk()
    st_screen.set_bg_color(hx(C_BG)); st_screen.set_text_color(hx(C_TEXT))
    st_screen.set_text_font(f_body()); st_screen.set_radius(0)
    st_screen.set_border_width(0); st_screen.set_pad_all(0)

    st_card = _mk()
    st_card.set_bg_color(hx(C_SURFACE)); st_card.set_radius(R_CARD)
    st_card.set_border_width(BORDER_HAIR); st_card.set_border_color(hx(C_DIVIDER))
    st_card.set_pad_all(PAD_M); st_card.set_text_color(hx(C_TEXT))

    st_card_sel = _mk()
    st_card_sel.set_bg_color(hx(C_SURFACE_2)); st_card_sel.set_radius(R_CARD)
    st_card_sel.set_border_width(FOCUS_RING); st_card_sel.set_border_color(hx(C_ACCENT))
    st_card_sel.set_pad_all(PAD_M); st_card_sel.set_text_color(hx(C_TEXT))

    st_sidebar = _mk()
    st_sidebar.set_bg_color(hx(C_SIDEBAR))
    st_sidebar.set_bg_grad_color(hx(C_SIDEBAR_2))
    st_sidebar.set_bg_grad_dir(lvgl.GRAD_DIR.VER)
    st_sidebar.set_radius(0); st_sidebar.set_border_width(0); st_sidebar.set_pad_all(0)

    st_header = _mk()
    st_header.set_bg_color(hx(C_SURFACE)); st_header.set_radius(0)
    st_header.set_border_width(0); st_header.set_pad_left(PAD_M); st_header.set_pad_right(PAD_M)
    st_header.set_text_color(hx(C_TEXT))

    st_footer = _mk()
    st_footer.set_bg_color(hx(C_SURFACE)); st_footer.set_radius(0)
    st_footer.set_border_width(0); st_footer.set_pad_left(PAD_M); st_footer.set_pad_right(PAD_M)
    st_footer.set_text_color(hx(C_TEXT_DIM))

    st_bubble_me = _mk()
    st_bubble_me.set_bg_color(hx(C_ACCENT)); st_bubble_me.set_text_color(hx(C_ON_ACCENT))
    st_bubble_me.set_radius(R_BUBBLE); st_bubble_me.set_border_width(0)
    st_bubble_me.set_pad_top(3); st_bubble_me.set_pad_bottom(3)
    st_bubble_me.set_pad_left(8); st_bubble_me.set_pad_right(8)

    st_bubble_them = _mk()
    st_bubble_them.set_bg_color(hx(C_SURFACE_2)); st_bubble_them.set_text_color(hx(C_TEXT))
    st_bubble_them.set_radius(R_BUBBLE); st_bubble_them.set_border_width(0)
    st_bubble_them.set_pad_top(3); st_bubble_them.set_pad_bottom(3)
    st_bubble_them.set_pad_left(8); st_bubble_them.set_pad_right(8)

    st_input = _mk()
    st_input.set_bg_color(hx(C_SURFACE_2)); st_input.set_text_color(hx(C_TEXT))
    st_input.set_radius(R_PILL); st_input.set_border_width(BORDER_HAIR)
    st_input.set_border_color(hx(C_DIVIDER)); st_input.set_pad_left(8); st_input.set_pad_right(8)
    st_input.set_pad_top(1); st_input.set_pad_bottom(1)

    st_hint = _mk()
    st_hint.set_text_font(f_tiny()); st_hint.set_text_color(hx(C_TEXT_DIM))
    st_hint.set_bg_opa(0); st_hint.set_border_width(0); st_hint.set_pad_all(0)

    st_title = _mk()
    st_title.set_text_font(f_title()); st_title.set_text_color(hx(C_TEXT)); st_title.set_bg_opa(0)

    st_value = _mk()
    st_value.set_text_font(f_tiny()); st_value.set_text_color(hx(C_ACCENT)); st_value.set_bg_opa(0)

    _inited = True
