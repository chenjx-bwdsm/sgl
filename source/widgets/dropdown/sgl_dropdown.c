/* source/widgets/sgl_dropdown.c
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

#include <sgl_core.h>
#include <sgl_draw.h>
#include <sgl_math.h>
#include <sgl_log.h>
#include <sgl_mm.h>
#include <sgl_cfgfix.h>
#include <sgl_theme.h>
#include "sgl_dropdown.h"

static const uint8_t dropdown_bitmap[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x00,
    0x0c,0xfa,0x00,0x00,0x00,0x00,0x05,0xee,0x50,
    0x0c,0xff,0xa0,0x00,0x00,0x00,0x5e,0xfe,0x30,
    0x00,0xcf,0xfa,0x00,0x00,0x05,0xef,0xe3,0x00,
    0x00,0x0c,0xff,0xa0,0x00,0x5e,0xfe,0x30,0x00,
    0x00,0x00,0xcf,0xfa,0x05,0xef,0xe3,0x00,0x00,
    0x00,0x00,0x0c,0xff,0xae,0xfe,0x30,0x00,0x00,
    0x00,0x00,0x00,0xcf,0xff,0xe3,0x00,0x00,0x00,
    0x00,0x00,0x00,0x0c,0xfe,0x30,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x53,0x00,0x00,0x00,0x00,
};


static const sgl_icon_pixmap_t dropdown_icon = { 
    .bitmap = dropdown_bitmap,
    .height = 10,
    .width = 18,
};


static void sgl_dropdown_construct_cb(sgl_surf_t *surf, sgl_obj_t* obj, sgl_event_t *evt)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    int16_t icon_h = dropdown->option_h;
    int16_t pos_x = 0, pos_y = 0, icon_y = 0;
    sgl_dropdown_option_t *option = dropdown->head;

    sgl_rect_t body_area = {
        .x1 = obj->coords.x1,
        .x2 = obj->coords.x2,
        .y1 = obj->coords.y1,
        .y2 = obj->coords.y1 + dropdown->option_h,
    };

    sgl_rect_t icon_area = {
        .x1 = obj->coords.x2 - dropdown_icon.width - dropdown->body_desc.radius,
        .x2 = obj->coords.x2 - 2,
        .y1 = obj->coords.y1,
        .y2 = obj->coords.y2,
    };

    sgl_rect_t text_area = {
        .x1 = obj->coords.x1 + 2,
        .x2 = obj->coords.x2 - dropdown_icon.width - 5,
        .y1 = obj->coords.y1,
        .y2 = obj->coords.y2,
    };

    icon_y = obj->coords.y1 + (icon_h - dropdown_icon.height) / 2;

    if (evt->type == SGL_EVENT_DRAW_MAIN) {
        sgl_draw_rect(surf, &obj->area, &body_area, &dropdown->body_desc);
        if (dropdown->is_open) {
            sgl_draw_icon(surf, &obj->area, icon_area.x1, icon_y + 2, dropdown->text_color, dropdown->body_desc.alpha, &dropdown_icon);
        }
        else {
            sgl_draw_icon(surf, &obj->area, icon_area.x1, icon_y, dropdown->text_color, dropdown->body_desc.alpha, &dropdown_icon);
        }

        for (int i = 0; option != NULL; i++) {
            if (i == dropdown->selected) {
                break;
            }
            option = option->next;
        }

        if (option != NULL) {
            pos_x = text_area.x1 + dropdown->body_desc.radius;
            pos_y = obj->coords.y1 + (icon_h - dropdown->font->font_height) / 2;
            sgl_draw_string(surf, &text_area, pos_x, pos_y, option->text, dropdown->text_color, dropdown->body_desc.alpha, dropdown->font);
        }

        if (dropdown->is_open) {
            pos_y = body_area.y2;
            option = dropdown->expend_start;
            body_area.y1 = obj->coords.y1 + dropdown->option_h;
            body_area.y2 = body_area.y1 + dropdown->expand_h - 1;
            sgl_draw_rect(surf, &obj->area, &body_area, &dropdown->body_desc);

            for (int i = 0; option != NULL; i++) {
                pos_x = text_area.x1 + dropdown->body_desc.radius;
                sgl_draw_string(surf, &obj->area, pos_x, pos_y, option->text, dropdown->text_color, dropdown->body_desc.alpha, dropdown->font);
                option = option->next;
                pos_y += dropdown->font->font_height;
            }
        }
    }
    else if (evt->type == SGL_EVENT_MOVE_UP) {
        
    }
    else if (evt->type == SGL_EVENT_MOVE_DOWN) {

    }
    else if (evt->type == SGL_EVENT_CLICKED) {
        dropdown->expand_h = dropdown->font->font_height * sgl_min(10, dropdown->option_num);

        if (dropdown->is_open) {
            dropdown->is_open = false;
            obj->coords.y2 -= dropdown->expand_h;
            if (evt->pos.y > obj->coords.y2) {
                dropdown->selected = (evt->pos.y - obj->coords.y2) / dropdown->font->font_height;
            }
        }
        else {
            dropdown->is_open = true;
            obj->coords.y2 += dropdown->expand_h;
        }

        sgl_obj_set_dirty(obj);
        dropdown->expend_start = dropdown->head;
    }
    else if (evt->type == SGL_EVENT_DRAW_INIT) {
        dropdown->option_h = obj->coords.y2 - obj->coords.y1;
    }
}


/**
 * @brief create a dropdown object
 * @param parent parent of the dropdown
 * @return pointer to the dropdown object
 */
sgl_obj_t* sgl_dropdown_create(sgl_obj_t* parent)
{
    sgl_dropdown_t *dropdown = sgl_malloc(sizeof(sgl_dropdown_t));
    if(dropdown == NULL) {
        SGL_LOG_ERROR("sgl_dropdown_create: malloc failed");
        return NULL;
    }

    /* set object all member to zero */
    memset(dropdown, 0, sizeof(sgl_dropdown_t));

    sgl_obj_t *obj = &dropdown->obj;
    sgl_obj_init(&dropdown->obj, parent);
    obj->construct_fn = sgl_dropdown_construct_cb;
    obj->needinit = 1;
    sgl_obj_set_border_width(obj, SGL_THEME_BORDER_WIDTH);

    sgl_obj_set_clickable(obj);
    sgl_obj_set_movable(obj);

    dropdown->body_desc.alpha = SGL_THEME_ALPHA;
    dropdown->body_desc.border_alpha = SGL_THEME_ALPHA;
    dropdown->body_desc.color = SGL_THEME_COLOR;
    dropdown->body_desc.radius = 5;
    dropdown->body_desc.border = 1;
    dropdown->body_desc.border_color = SGL_THEME_BORDER_COLOR;

    dropdown->font = sgl_get_system_font();
    dropdown->head = NULL;
    dropdown->is_open = false;

    dropdown->selected = -1;
    dropdown->clicked = 0;

    return obj;
}

/**
 * @brief set dropdown object's color
 * @param obj dropdown object
 * @param color color
 */
void sgl_dropdown_set_color(sgl_obj_t *obj, sgl_color_t color)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->body_desc.color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's border width
 * @param obj dropdown object
 * @param width border width
 */
void sgl_dropdown_set_border_width(sgl_obj_t *obj, uint8_t width)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->body_desc.border = width;
    sgl_obj_set_border_width(obj, width);
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's border color
 * @param obj dropdown object
 * @param color border color
 */
void sgl_dropdown_set_border_color(sgl_obj_t *obj, sgl_color_t color)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->body_desc.border_color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's radius
 * @param obj dropdown object
 * @param radius radius
 */
void sgl_dropdown_set_radius(sgl_obj_t *obj, uint8_t radius)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    sgl_obj_set_radius(obj, radius);
    dropdown->body_desc.radius = obj->radius;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's pixmap
 * @param obj dropdown object
 * @param pixmap pixmap
 */
void sgl_dropdown_set_pixmap(sgl_obj_t *obj, const sgl_pixmap_t *pixmap)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->body_desc.pixmap = pixmap;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's alpha
 * @param obj dropdown object
 * @param alpha alpha
 */
void sgl_dropdown_set_alpha(sgl_obj_t *obj, uint8_t alpha)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->body_desc.alpha = alpha;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's text color
 * @param obj dropdown object
 * @param color text color
 */
void sgl_dropdown_set_text_color(sgl_obj_t *obj, sgl_color_t color)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->text_color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's text font
 * @param obj dropdown object
 * @param font text font
 * @return none
 * @note font must be initialized
 */
void sgl_dropdown_set_text_font(sgl_obj_t *obj, const sgl_font_t* font)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->font = font;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's selected index
 * @param obj dropdown object
 * @param index selected index
 * @return none
 */
void sgl_dropdown_set_selected_index(sgl_obj_t *obj, int index)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    SGL_ASSERT(obj != NULL && index >= 0 && index < dropdown->option_num);
    dropdown->selected = index;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief get dropdown object's selected index
 * @param obj dropdown object
 * @return selected index
 */
int sgl_dropdown_get_selected_index(sgl_obj_t *obj)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    return dropdown->selected;
}

/**
 * @brief get the selected text of the dropdown
 * @param obj pointer to the dropdown object
 * @return pointer to the selected text
 */
const char *sgl_dropdown_get_selected_text(sgl_obj_t *obj)
{
    sgl_dropdown_t *dropdown = (sgl_dropdown_t*)obj;
    sgl_dropdown_option_t *option = dropdown->head;

    for (int i = 0; i < dropdown->option_num; i++) {
        if (i == dropdown->selected) {
            return option->text;
        }
        option = option->next;
    }
    return NULL;
}

/**
 * @brief add an option to the dropdown
 * @param obj pointer to the dropdown object
 * @param text pointer to the text
 * @return none
 */
void sgl_dropdown_add_option(sgl_obj_t *obj, const char *text)
{
    sgl_dropdown_t *dropdown = (sgl_dropdown_t*)obj;
    sgl_dropdown_option_t *tail = dropdown->head;
    sgl_dropdown_option_t *add = sgl_malloc(sizeof(sgl_dropdown_option_t));

    if (add == NULL) {
        SGL_LOG_ERROR("sgl_dropdown_add_option: malloc failed");
        return;
    }

    if (tail == NULL) {
        dropdown->head = add;
    }
    else {
        while (tail != NULL) {
            if (tail->next == NULL) {
                tail->next = add;
                break;
            }
            tail = (tail)->next;
        }
    }

    dropdown->option_num ++;
    add->text = text;
    add->next = NULL;

    if (dropdown->selected == -1) {
        dropdown->selected = 0;
    }

    sgl_obj_set_dirty(obj);
}

/**
 * @brief delete an option from the dropdown
 * @param obj pointer to the dropdown object
 * @param text pointer to the text
 * @return none
 */
void sgl_dropdown_delete_option_by_text(sgl_obj_t *obj, const char *text)
{
    sgl_dropdown_t *dropdown = (sgl_dropdown_t*)obj;
    sgl_dropdown_option_t *curr = dropdown->head;
    sgl_dropdown_option_t *temp = dropdown->head;
    sgl_dropdown_option_t *prev = NULL;
    int deleted_index = 0;

    if (obj == NULL || text == NULL) {
        return;
    }

    while (curr != NULL) {
        if (curr->text && strcmp(curr->text, text) == 0) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL) {
        return;
    }

    while (temp != curr) {
        temp = temp->next;
        deleted_index ++;
    }

    if (prev == NULL) {
        dropdown->head = curr->next;
    } else {
        prev->next = curr->next;
    }

    sgl_free(curr);
    dropdown->option_num--;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief delete an option from the dropdown
 * @param obj pointer to the dropdown object
 * @param index index of the option
 * @return none
 */
void sgl_dropdown_delete_option_by_index(sgl_obj_t *obj, int index)
{
    sgl_dropdown_t *dropdown = (sgl_dropdown_t*)obj;
    sgl_dropdown_option_t *curr = dropdown->head;
    sgl_dropdown_option_t *prev = NULL;

    if (obj == NULL || index < 0 || index >= dropdown->option_num) {
        return;
    }

    for (int i = 0; i < index; i++) {
        prev = curr;
        curr = curr->next;
    }

    if (prev == NULL) {
        dropdown->head = curr->next;
    } else {
        prev->next = curr->next;
    }

    sgl_free(curr);
    dropdown->option_num--;
}
