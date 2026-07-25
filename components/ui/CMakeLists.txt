idf_component_register(
    SRCS "portable/theme_map.c" "theme.c" "frame.c" "sidebar.c" "menubar.c"
         "launcher_strip.c" "icons.c" "map_canvas.c"
    INCLUDE_DIRS "include"
    REQUIRES "lvgl" "core" "net" "mesh" "util" "bsp")
