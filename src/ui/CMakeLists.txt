SET(SOURCES screens/ui_MainScreen.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_jazz_png.c
    images/ui_img_play_png.c
    images/ui_img_pause_png.c)

add_library(ui ${SOURCES})
