"""On-device LVGL UI probe.

Exercises every LVGL call/enum the new UI relies on whose availability on this
specific frozen binary was NOT verified from existing code, so a single run tells
us exactly what renders. Non-destructive: it builds objects on a scratch screen
that is never loaded, plus one temp child on layer_top that it deletes.

Run it after the badge has booted (so LVGL is initialised):

    # interrupt the running firmware first: press Ctrl-C once in the serial REPL
    mpremote run firmware/scripts/ui_probe.py

Then paste the whole "UI PROBE RESULTS" block back.
"""
import lvgl

_results = []


def check(name, fn):
    try:
        r = fn()
        _results.append(("OK  ", name, "" if r is None else str(r)))
    except Exception as e:  # noqa: BLE001
        _results.append(("FAIL", name, repr(e)))


# --- enums / module attributes --------------------------------------------
check("lvgl.layer_top()", lambda: "ok" if lvgl.layer_top() is not None else "none")
check("GRAD_DIR.VER", lambda: lvgl.GRAD_DIR.VER)
check("ALIGN.LEFT_MID", lambda: lvgl.ALIGN.LEFT_MID)
check("ALIGN.RIGHT_MID", lambda: lvgl.ALIGN.RIGHT_MID)
check("ALIGN.TOP_MID", lambda: lvgl.ALIGN.TOP_MID)
check("ALIGN.BOTTOM_MID", lambda: lvgl.ALIGN.BOTTOM_MID)
check("ALIGN.CENTER", lambda: lvgl.ALIGN.CENTER)
check("FLEX_FLOW.COLUMN", lambda: lvgl.FLEX_FLOW.COLUMN)
check("FLEX_FLOW.ROW", lambda: lvgl.FLEX_FLOW.ROW)
check("FLEX_ALIGN.START", lambda: lvgl.FLEX_ALIGN.START)
check("PART.CURSOR", lambda: lvgl.PART.CURSOR)
check("STATE.FOCUSED", lambda: lvgl.STATE.FOCUSED)
check("pct(50)", lambda: lvgl.pct(50))
check("font_montserrat_28", lambda: "ok" if lvgl.font_montserrat_28 else "none")
check("font_montserrat_42", lambda: "ok" if lvgl.font_montserrat_42 else "none")


# --- style_t setters (theme.init_styles uses these) -----------------------
def style_setters():
    s = lvgl.style_t()
    s.init()
    s.set_bg_color(lvgl.color_hex(0x101418))
    s.set_text_color(lvgl.color_hex(0xF0F0F0))
    s.set_radius(8)
    s.set_border_width(1)
    s.set_border_color(lvgl.color_hex(0x303030))
    s.set_pad_all(8)
    s.set_pad_top(3); s.set_pad_bottom(3); s.set_pad_left(8); s.set_pad_right(8)
    s.set_text_font(lvgl.font_montserrat_14)
    s.set_bg_opa(255)
    return "ok"


check("style_t basic setters", style_setters)


def style_gradient():
    s = lvgl.style_t()
    s.init()
    s.set_bg_color(lvgl.color_hex(0x0A0D11))
    s.set_bg_grad_color(lvgl.color_hex(0x131922))
    s.set_bg_grad_dir(lvgl.GRAD_DIR.VER)
    return "ok"


check("style_t gradient (sidebar)", style_gradient)

# Scratch screen (created but NEVER loaded -> display unchanged).
_scr = lvgl.obj()


def obj_style_setters():
    o = lvgl.obj(_scr)
    o.set_pos(5, 5)
    o.set_size(20, 12)
    o.set_scrollbar_mode(0)
    o.set_style_bg_color(lvgl.color_hex(0x123456), 0)
    o.set_style_bg_opa(255, 0)
    o.set_style_border_width(1, 0)
    o.set_style_border_color(lvgl.color_hex(0xFFFFFF), 0)
    o.set_style_radius(6, 0)
    o.set_style_pad_all(0, 0)
    o.set_style_opa(128, 0)
    return "ok"


check("obj set_pos/size/style_*", obj_style_setters)


def obj_gradient():
    o = lvgl.obj(_scr)
    o.set_style_bg_color(lvgl.color_hex(0xE39810), 0)
    o.set_style_bg_grad_color(lvgl.color_hex(0x8A5C06), 0)
    o.set_style_bg_grad_dir(lvgl.GRAD_DIR.VER, 0)
    return "ok"


check("obj-level gradient (app_glyph)", obj_gradient)


def flex_ops():
    o = lvgl.obj(_scr)
    o.set_flex_flow(lvgl.FLEX_FLOW.COLUMN)
    o.set_flex_align(lvgl.FLEX_ALIGN.START, lvgl.FLEX_ALIGN.START, lvgl.FLEX_ALIGN.START)
    c = lvgl.obj(o)
    c.set_flex_grow(1)
    return "ok"


check("flex flow/align/grow", flex_ops)


def label_text_align():
    lab = lvgl.label(_scr)
    lab.set_text("x")
    lab.set_style_text_align(lvgl.ALIGN.CENTER, 0)
    lab.set_style_text_font(lvgl.font_montserrat_28, 0)
    return "ok"


check("label text_align + big font", label_text_align)


def label_wrap_autoheight():
    lab = lvgl.label(_scr)
    lab.set_width(60)
    lab.set_text("this is a long message that should wrap onto several lines here")
    _scr.update_layout()
    h = lab.get_height()
    return "w=%d h=%d -> %s" % (lab.get_width(), h, "WRAPS" if h > 22 else "NO-WRAP(!)")


check("label wrap + auto-height (chat bubbles)", label_wrap_autoheight)


def styled_label_bubble():
    s = lvgl.style_t()
    s.init()
    s.set_bg_color(lvgl.color_hex(0x222A35))
    s.set_radius(10)
    s.set_pad_left(8); s.set_pad_right(8); s.set_pad_top(3); s.set_pad_bottom(3)
    lab = lvgl.label(_scr)
    lab.add_style(s, 0)
    lab.set_text("hi")
    _scr.update_layout()
    return "hug w=%d h=%d" % (lab.get_width(), lab.get_height())


check("styled-label hugging bubble", styled_label_bubble)


def textarea_ops():
    ta = lvgl.textarea(_scr)
    ta.set_one_line(True)
    ta.add_state(lvgl.STATE.FOCUSED)
    ta.set_text("")
    ta.add_text("abc")
    ta.cursor_left()
    ta.cursor_right()
    ta.delete_char()
    ta.delete_char_forward()
    ta.set_style_border_color(lvgl.color_hex(0xE39810), lvgl.PART.CURSOR | lvgl.STATE.FOCUSED)
    return "text='%s'" % ta.get_text()


check("textarea full input API", textarea_ops)


def scroll_ops():
    o = lvgl.obj(_scr)
    o.set_size(50, 30)
    o.set_scrollbar_mode(0)
    for i in range(8):
        c = lvgl.obj(o)
        c.set_pos(0, i * 20)
        c.set_size(40, 15)
    _scr.update_layout()
    sb = o.get_scroll_bottom()
    o.scroll_by_bounded(0, -5, False)
    return "scroll_bottom=%s" % sb


check("scroll_by_bounded / get_scroll_bottom", scroll_ops)


def layer_top_child():
    top = lvgl.layer_top()
    t = lvgl.obj(top)
    t.set_size(4, 4)
    t.set_pos(0, 0)
    t.delete()
    return "ok"


check("layer_top add/delete child", layer_top_child)

# Clean up the scratch screen (was never loaded).
try:
    _scr.delete()
except Exception:  # noqa: BLE001
    pass

print("\n==== UI PROBE RESULTS (paste this whole block back) ====")
_fails = 0
for status, name, detail in _results:
    if status == "FAIL":
        _fails += 1
    print("[%s] %-34s %s" % (status, name, detail))
print("---- %d checks, %d FAIL ----" % (len(_results), _fails))
print("==== END UI PROBE ====")
