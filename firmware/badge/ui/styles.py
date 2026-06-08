"""Legacy style objects, re-pointed to the dark theme (ui/theme.py).

Kept so the legacy Page widget and any code referencing these names keep working
during the UI migration; new screens use ui/theme.py + ui/frame.py directly.
"""
import lvgl

from ui import theme

_hx = lvgl.color_hex

# Legacy palette names, now mapped onto the dark theme tokens.
lcd_color_bg = _hx(theme.C_BG)
lcd_color_fg = _hx(theme.C_TEXT)
lcd_color_fg_dark = _hx(theme.C_TEXT_DIM)

accent = _hx(theme.C_ACCENT)
bg_dark = _hx(theme.C_BG)
fg_white = _hx(theme.C_TEXT)

# Back-compat brand aliases.
hackaday_grey = bg_dark
hackaday_yellow = accent
hackaday_white = fg_white


def _style(font, bg, fg):
    s = lvgl.style_t()
    s.init()
    s.set_text_font(font)
    s.set_bg_color(bg)
    s.set_text_color(fg)
    s.set_radius(0)
    s.set_border_width(0)
    s.set_pad_all(0)
    return s


base_style = _style(lvgl.font_montserrat_12, _hx(theme.C_BG), _hx(theme.C_TEXT))
content_style = _style(lvgl.font_montserrat_12, _hx(theme.C_BG), _hx(theme.C_TEXT))
menubar_style = _style(lvgl.font_montserrat_16, _hx(theme.C_SURFACE), _hx(theme.C_TEXT))
infobar_style = _style(lvgl.font_montserrat_14, _hx(theme.C_SURFACE), _hx(theme.C_TEXT_DIM))

lvg_color_black = _hx(0x000000)
lvg_color_red = _hx(theme.C_CRIT)
lvg_color_green = _hx(theme.C_OK)
