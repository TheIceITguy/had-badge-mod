idf_component_register(
    SRCS "portable/theme_map.c" "theme.c" "frame.c" "sidebar.c" "menubar.c"
         "launcher_strip.c" "icons.c" "map_canvas.c"
         "fonts/lv_font_montserrat_is_14.c" "fonts/lv_font_montserrat_is_18.c"
    INCLUDE_DIRS "include"
    REQUIRES "lvgl" "core" "net" "mesh" "util" "bsp")
