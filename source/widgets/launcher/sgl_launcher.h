#ifndef __SGL_LAUNCHER_H__
#define __SGL_LAUNCHER_H__

#include <sgl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sgl_launcher {
    sgl_obj_t obj;
    int16_t icon_size;
    int16_t margin_left;
    int16_t margin_top;
    int16_t margin_right;
    int16_t margin_bottom;
    int16_t grid_row;
    int16_t grid_col;
    int16_t page_width;
    int16_t page_height;
    int16_t drag_start_x;
    uint8_t count;
    uint8_t page_count;
    sgl_color_t navigbar_color;
    sgl_color_t label_color;
    const sgl_font_t *font;
    sgl_obj_t *statusbar;
} sgl_launcher_t;

typedef struct sgl_launcher_app {
    void (*event_cb)(sgl_event_t *evt);
    void *private_data;
    const sgl_pixmap_t *icon;
    const char *name;
} sgl_launcher_app_t;

/**
 * @brief create a launcher object
 * @param parent parent object
 * @return launcher object
 */
sgl_obj_t *sgl_launcher_create(sgl_obj_t *parent);

/**
 * @brief get launcher statusbar
 * @param launcher the launcher object
 * @return statusbar object
 */
sgl_obj_t* sgl_launcher_statusbar(sgl_obj_t *launcher);

/**
 * @brief set launcher font
 * @param launcher the launcher object
 * @param font the font
 * @return none
 */
void sgl_launcher_set_font(sgl_obj_t *launcher, const sgl_font_t *font);

/**
 * @brief set launcher margin
 * @param launcher the launcher object
 * @param left left margin
 * @param top top margin
 * @param right right margin
 * @param bottom bottom margin
 * @return none
 */
void sgl_launcher_set_margin(sgl_obj_t *launcher, int16_t left, int16_t top, int16_t right, int16_t bottom);

/**
 * @brief set launcher icon size
 * @param launcher the launcher object
 * @param size the icon size
 * @return none
 */
void sgl_launcher_set_icon_size(sgl_obj_t *launcher, int16_t size);

/**
 * @brief set launcher grid size
 * @param launcher the launcher object
 * @param cols the number of columns
 * @param rows the number of rows
 * @return none
 */
void sgl_launcher_set_grid_size(sgl_obj_t *launcher, int16_t cols, int16_t rows);

/**
 * @brief add an app to launcher
 * @param launcher the launcher object
 * @param app the app
 * @return none
 */
void sgl_launcher_add_app(sgl_obj_t *launcher, sgl_launcher_app_t *app);

/**
 * @brief set launcher label color
 * @param launcher the launcher object
 * @param color the label color
 * @return none
 */
void sgl_launcher_set_label_color(sgl_obj_t *launcher, sgl_color_t color);

/**
 * @brief set launcher navigation bar color
 * @param launcher the launcher object
 * @param color the navigation bar color
 * @return none
 */
void sgl_launcher_set_navigbar_color(sgl_obj_t *launcher, sgl_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_LAUNCHER_H__ */
