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
    uint8_t count;
    uint8_t page_count;
    const sgl_font_t *font;
    sgl_obj_t *statusbar;
} sgl_launcher_t;

typedef struct sgl_launcher_app {
    void (*launch)(void *data);
    void *private_data;
    const sgl_pixmap_t *icon;
    const char *name;
} sgl_launcher_app_t;

sgl_obj_t *sgl_launcher_create(sgl_obj_t *parent);
void sgl_launcher_set_font(sgl_obj_t *launcher, const sgl_font_t *font);
void sgl_launcher_add_app(sgl_obj_t *launcher, sgl_launcher_app_t *app);
sgl_obj_t* sgl_launcher_statusbar(sgl_obj_t *launcher);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_LAUNCHER_H__ */
