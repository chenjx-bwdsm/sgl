/* source/widgets/Statusbar/sgl_statusbar.h
 *
 * MIT License
 *
 * Copyright(c) 2023-present All contributors of SGL
 * Document reference link: https://sgl-docs.readthedocs.io
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __SGL_STATUSBAR_H__
#define __SGL_STATUSBAR_H__

#include <sgl_core.h>
#include <sgl_draw.h>
#include <sgl_log.h>
#include <sgl_mm.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SGL_STATUSBAR_LEFT_MAX       (4)
#define SGL_STATUSBAR_RIGHT_MAX      (8)

typedef struct sgl_statusbar_icon {
    char icon[4];
    sgl_color_t color;
    uint8_t alpha;
    uint8_t reserved;
} sgl_statusbar_icon_t;

typedef struct sgl_statusbar {
    sgl_obj_t obj;
    const sgl_font_t *font;
    sgl_statusbar_icon_t icon_left[SGL_STATUSBAR_LEFT_MAX];
    sgl_statusbar_icon_t icon_right[SGL_STATUSBAR_RIGHT_MAX];
    uint8_t icon_left_margin;
    uint8_t icon_right_margin;
    sgl_color_t bg_color;
    uint8_t icon_space;
    uint8_t bg_alpha;
} sgl_statusbar_t;

/**
 * @brief create Statusbar widget
 * @param parent parent object
 * @return Statusbar object
 */
sgl_obj_t* sgl_statusbar_create(sgl_obj_t *parent);


/**
 * @brief Set statusbar font
 * @param obj statusbar object
 * @param font font
 */
void sgl_statusbar_set_font(sgl_obj_t *obj, const sgl_font_t *font);

/**
 * @brief Set statusbar background color
 * @param obj statusbar object
 * @param color background color
 */
void sgl_statusbar_set_bg_color(sgl_obj_t *obj, sgl_color_t color);

/**
 * @brief Set statusbar background alpha
 * @param obj statusbar object
 * @param alpha background alpha
 */
void sgl_statusbar_set_bg_alpha(sgl_obj_t *obj, uint8_t alpha);

/**
 * @brief Set left icon
 * @param obj statusbar object
 * @param index icon index
 * @param icon icon, it is a UTF8 code
 */
void sgl_statusbar_set_left_icon(sgl_obj_t *obj, uint8_t index, const char* icon);

/**
 * @brief Set right icon
 * @param obj statusbar object
 * @param index icon index
 * @param icon icon, it is a UTF8 code
 */
void sgl_statusbar_set_right_icon(sgl_obj_t *obj, uint8_t index, const char* icon);

/**
 * @brief Set left icon alpha
 * @param obj statusbar object
 * @param index icon index
 * @param alpha icon alpha
 */
void sgl_statusbar_set_left_icon_alpha(sgl_obj_t *obj, uint8_t index, uint8_t alpha);

/**
 * @brief Set right icon alpha
 * @param obj statusbar object
 * @param index icon index
 * @param alpha icon alpha
 */
void sgl_statusbar_set_right_icon_alpha(sgl_obj_t *obj, uint8_t index, uint8_t alpha);

/**
 * @brief Set left icon color
 * @param obj statusbar object
 * @param index icon index
 * @param color icon color
 */
void sgl_statusbar_set_left_icon_color(sgl_obj_t *obj, uint8_t index, sgl_color_t color);

/**
 * @brief Set right icon color
 * @param obj statusbar object
 * @param index icon index
 * @param color icon color
 */
void sgl_statusbar_set_right_icon_color(sgl_obj_t *obj, uint8_t index, sgl_color_t color);

/**
 * @brief Set icon space
 * @param obj statusbar object
 * @param space icon space
 */
void sgl_statusbar_set_icon_space(sgl_obj_t *obj, uint8_t space);

/**
 * @brief Set icon margin
 * @param obj statusbar object
 * @param left left icon margin
 * @param right right icon margin
 */
void sgl_statusbar_set_icon_margin(sgl_obj_t *obj, uint8_t left, uint8_t right);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_STATUSBAR_H__ */
