"""Frame: the shared screen chrome every app builds in switch_to_foreground.

Guarantees the status-sidebar inset (content starts at x=SIDEBAR_W) so nothing
overlaps the persistent sidebar. Provides a header (title + context), a scrollable
body, and an optional footer that is either an F-key hint bar or an always-on
text input (for chat / inline editing).
"""
import lvgl

from ui import theme

_hx = lvgl.color_hex


class Frame:
    def __init__(self, title="", context=""):
        theme.init_styles()
        self.scr = lvgl.obj()
        self.scr.set_scrollbar_mode(0)
        self.scr.add_style(theme.st_screen, 0)

        self.col = lvgl.obj(self.scr)
        self.col.set_pos(theme.CONTENT_X, 0)
        self.col.set_size(theme.CONTENT_W, theme.SCREEN_H)
        self.col.set_scrollbar_mode(0)
        self.col.add_style(theme.st_screen, 0)
        self.col.set_style_pad_all(0, 0)
        self.col.set_flex_flow(lvgl.FLEX_FLOW.COLUMN)
        self.col.set_flex_align(lvgl.FLEX_ALIGN.START, lvgl.FLEX_ALIGN.START,
                                lvgl.FLEX_ALIGN.START)

        self._build_header(title, context)
        self.body = self._build_body()
        self.footer = None
        self.input = None
        self._ph = None

    # --- header ---------------------------------------------------------
    def _build_header(self, title, context):
        self.header = lvgl.obj(self.col)
        self.header.set_size(theme.CONTENT_W, theme.HEADER_H)
        self.header.set_scrollbar_mode(0)
        self.header.add_style(theme.st_header, 0)
        self.title_lbl = lvgl.label(self.header)
        self.title_lbl.add_style(theme.st_title, 0)
        self.title_lbl.align(lvgl.ALIGN.LEFT_MID, 0, 0)
        self.title_lbl.set_text(title)
        self.ctx_lbl = lvgl.label(self.header)
        self.ctx_lbl.set_style_text_font(theme.f_tiny(), 0)
        self.ctx_lbl.set_style_text_color(_hx(theme.C_TEXT_DIM), 0)
        self.ctx_lbl.align(lvgl.ALIGN.RIGHT_MID, 0, 0)
        self.ctx_lbl.set_text(context)

    def set_title(self, s):
        self.title_lbl.set_text(s)

    def set_context(self, s):
        self.ctx_lbl.set_text(s)

    # --- body -----------------------------------------------------------
    def _build_body(self):
        b = lvgl.obj(self.col)
        b.set_width(theme.CONTENT_W)
        b.set_flex_grow(1)
        b.set_scrollbar_mode(0)
        b.add_style(theme.st_screen, 0)
        b.set_style_pad_left(theme.PAD_M, 0)
        b.set_style_pad_right(theme.PAD_M, 0)
        b.set_style_pad_top(theme.PAD_S, 0)
        b.set_style_pad_bottom(theme.PAD_S, 0)
        return b

    # --- footer: F-key hint bar ----------------------------------------
    def make_menubar(self, labels):
        self.footer = lvgl.obj(self.col)
        self.footer.set_size(theme.CONTENT_W, theme.FOOTER_H)
        self.footer.set_scrollbar_mode(0)
        self.footer.add_style(theme.st_footer, 0)
        self._menu_labels = []
        cell = theme.CONTENT_W // 5
        for i in range(5):
            lbl = lvgl.label(self.footer)
            lbl.set_style_text_font(theme.f_tiny(), 0)
            txt = labels[i] if i < len(labels) else ""
            lbl.set_style_text_color(_hx(theme.C_ACCENT if txt else theme.C_TEXT_MUTE), 0)
            lbl.set_text(("F%d %s" % (i + 1, txt)) if txt else "")
            lbl.align(lvgl.ALIGN.LEFT_MID, i * cell + 2, 0)
            self._menu_labels.append(lbl)
        return self.footer

    def set_menu_label(self, i, text):
        lbl = self._menu_labels[i]
        lbl.set_text(("F%d %s" % (i + 1, text)) if text else "")
        lbl.set_style_text_color(_hx(theme.C_ACCENT if text else theme.C_TEXT_MUTE), 0)

    # --- footer: always-on text input ----------------------------------
    def make_input(self, placeholder="Message"):
        self.footer = lvgl.obj(self.col)
        self.footer.set_size(theme.CONTENT_W, theme.FOOTER_H)
        self.footer.set_scrollbar_mode(0)
        self.footer.add_style(theme.st_footer, 0)
        self.input = lvgl.textarea(self.footer)
        self.input.set_one_line(True)
        self.input.add_style(theme.st_input, 0)
        self.input.set_size(theme.CONTENT_W - 2 * theme.PAD_M, theme.FOOTER_H - 4)
        self.input.align(lvgl.ALIGN.CENTER, 0, 0)
        self.input.set_style_border_color(_hx(theme.C_ACCENT),
                                          lvgl.PART.CURSOR | lvgl.STATE.FOCUSED)
        self.input.add_state(lvgl.STATE.FOCUSED)
        self.input.set_text("")
        self._ph = lvgl.label(self.footer)
        self._ph.set_text(placeholder)
        self._ph.set_style_text_font(theme.f_body(), 0)
        self._ph.set_style_text_color(_hx(theme.C_TEXT_MUTE), 0)
        self._ph.align(lvgl.ALIGN.LEFT_MID, theme.PAD_M + 6, 0)
        return self.input

    def _show_ph(self, show):
        if self._ph is not None:
            self._ph.set_style_opa(255 if show else 0, 0)

    def input_text(self):
        return self.input.get_text().strip() if self.input else ""

    def clear_input(self):
        if self.input:
            self.input.set_text("")
            self._show_ph(True)

    def input_add(self, ch):
        if self.input:
            self._show_ph(False)
            self.input.add_text(ch)

    def input_backspace(self):
        if self.input:
            self.input.delete_char()
            if not self.input.get_text():
                self._show_ph(True)

    def input_delete_fwd(self):
        if self.input:
            self.input.delete_char_forward()

    def cursor_left(self):
        if self.input:
            self.input.cursor_left()

    def cursor_right(self):
        if self.input:
            self.input.cursor_right()

    # --- screen swap ----------------------------------------------------
    def replace_screen(self):
        old = lvgl.screen_active()
        lvgl.screen_load(self.scr)
        old.delete()

    def delete(self):
        self.scr.delete()
