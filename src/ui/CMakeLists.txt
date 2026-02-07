SET(SOURCES screens/ui_MainScreen.c
    screens/ui_WiFiSettingsScreen.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_wifi_off_png.c
    images/ui_img_wifi_on_png.c
    images/ui_img_home_png.c)

add_library(ui ${SOURCES})
