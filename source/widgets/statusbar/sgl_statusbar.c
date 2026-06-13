/* source/widgets/Statusbar/sgl_statusbar.c
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
#include "sgl_statusbar.h"

static void sgl_statusbar_construct_cb(sgl_surf_t *surf, sgl_obj_t *obj, sgl_event_t *evt)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    uint32_t unicode, ch_index;
    int16_t pos_x, pos_y, char_w;

    switch (evt->type) {
    case SGL_EVENT_DRAW_MAIN:
        sgl_draw_fill_rect(surf, &obj->area, &obj->coords, obj->radius, bar->bg_color, bar->bg_alpha);

        pos_y = obj->coords.y1 + (sgl_obj_get_height(obj) - bar->font->font_height) / 2;
        pos_x = obj->coords.x1 + bar->icon_left_margin;

        for (int i = 0; i < SGL_STATUSBAR_LEFT_MAX; i++) {
            if (bar->icon_left[i].icon[0] == '\0') {
                continue;
            }

            sgl_utf8_to_unicode(bar->icon_left[i].icon, &unicode);
            ch_index = sgl_search_unicode_ch_index(bar->font, unicode);
            if (bar->icon_left[i].alpha != 0) {
                sgl_draw_character(surf, &obj->area, pos_x, pos_y, ch_index, bar->icon_left[i].color, bar->icon_left[i].alpha, bar->font);
            }

            pos_x += ((bar->font->table[ch_index].adv_w + 8)>> 4) + bar->icon_space;
        }

        pos_x = obj->coords.x2 - bar->icon_right_margin;
        for (int i = 0; i < SGL_STATUSBAR_RIGHT_MAX; i++) {
            if (bar->icon_right[i].icon[0] == '\0') {
                continue;
            }

            sgl_utf8_to_unicode(bar->icon_right[i].icon, &unicode);
            ch_index = sgl_search_unicode_ch_index(bar->font, unicode);
            char_w = ((bar->font->table[ch_index].adv_w + 8)>> 4);
            if (bar->icon_right[i].alpha != 0) {
                sgl_draw_character(surf, &obj->area, pos_x - char_w, pos_y, ch_index, bar->icon_right[i].color, bar->icon_right[i].alpha, bar->font);
            }
            pos_x -= (char_w + bar->icon_space);
        }
    break;
    default:
        break;
    };
}

/**
 * @brief Create a statusbar object
 * @param parent object parent
 * @return statusbar object
 */
sgl_obj_t* sgl_statusbar_create(sgl_obj_t *parent)
{
    sgl_statusbar_t *bar = sgl_malloc(sizeof(sgl_statusbar_t));
    if (bar == NULL) {
        SGL_LOG_ERROR("sgl_statusbar_create: malloc failed");
        return NULL;
    }

    /* set object all member to zero */
    memset(bar, 0, sizeof(sgl_statusbar_t));
    sgl_obj_t *obj = &bar->obj;
    sgl_obj_init(obj, parent);
    obj->construct_fn = sgl_statusbar_construct_cb;

    bar->bg_alpha = SGL_ALPHA_MAX / 2;
    bar->bg_color = sgl_rgb(20, 20, 20);
    bar->icon_left_margin = 5;
    bar->icon_right_margin = 5;
    bar->icon_space = 4;

    return obj;
}

/**
 * @brief Set statusbar font
 * @param obj statusbar object
 * @param font font
 */
void sgl_statusbar_set_font(sgl_obj_t *obj, const sgl_font_t *font)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    bar->font = font;
}

/**
 * @brief Set statusbar background color
 * @param obj statusbar object
 * @param color background color
 */
void sgl_statusbar_set_bg_color(sgl_obj_t *obj, sgl_color_t color)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    bar->bg_color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief Set statusbar background alpha
 * @param obj statusbar object
 * @param alpha background alpha
 */
void sgl_statusbar_set_bg_alpha(sgl_obj_t *obj, uint8_t alpha)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    bar->bg_alpha = alpha;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief Set left icon
 * @param obj statusbar object
 * @param index icon index
 * @param icon icon, it is a UTF8 code
 */
void sgl_statusbar_set_left_icon(sgl_obj_t *obj, uint8_t index, const char* icon)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_LEFT_MAX) {
        memcpy(&bar->icon_left[index].icon, icon, sizeof(bar->icon_left[0].icon));
        bar->icon_left[index].alpha = SGL_ALPHA_MAX;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set right icon
 * @param obj statusbar object
 * @param index icon index
 * @param icon icon, it is a UTF8 code
 */
void sgl_statusbar_set_right_icon(sgl_obj_t *obj, uint8_t index, const char* icon)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_RIGHT_MAX) {
        memcpy(&bar->icon_right[index].icon, icon, sizeof(bar->icon_right[0].icon));
        bar->icon_right[index].alpha = SGL_ALPHA_MAX;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set left icon alpha
 * @param obj statusbar object
 * @param index icon index
 * @param alpha icon alpha
 */
void sgl_statusbar_set_left_icon_alpha(sgl_obj_t *obj, uint8_t index, uint8_t alpha)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_LEFT_MAX) {
        bar->icon_left[index].alpha = alpha;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set right icon alpha
 * @param obj statusbar object
 * @param index icon index
 * @param alpha icon alpha
 */
void sgl_statusbar_set_right_icon_alpha(sgl_obj_t *obj, uint8_t index, uint8_t alpha)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_RIGHT_MAX) {
        bar->icon_right[index].alpha = alpha;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set left icon color
 * @param obj statusbar object
 * @param index icon index
 * @param color icon color
 */
void sgl_statusbar_set_left_icon_color(sgl_obj_t *obj, uint8_t index, sgl_color_t color)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_LEFT_MAX) {
        bar->icon_left[index].color = color;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set right icon color
 * @param obj statusbar object
 * @param index icon index
 * @param color icon color
 */
void sgl_statusbar_set_right_icon_color(sgl_obj_t *obj, uint8_t index, sgl_color_t color)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    if (index < SGL_STATUSBAR_LEFT_MAX) {
        bar->icon_right[index].color = color;
        sgl_obj_set_dirty(obj);
    }
}

/**
 * @brief Set icon space
 * @param obj statusbar object
 * @param space icon space
 */
void sgl_statusbar_set_icon_space(sgl_obj_t *obj, uint8_t space)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    bar->icon_space = space;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief Set icon margin
 * @param obj statusbar object
 * @param left left icon margin
 * @param right right icon margin
 */
void sgl_statusbar_set_icon_margin(sgl_obj_t *obj, uint8_t left, uint8_t right)
{
    sgl_statusbar_t *bar = sgl_container_of(obj, sgl_statusbar_t, obj);
    bar->icon_left_margin = left;
    bar->icon_right_margin = right;
    sgl_obj_set_dirty(obj);
}
