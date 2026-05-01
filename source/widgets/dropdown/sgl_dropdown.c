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

#define  SGL_DROPDOWN_OPTION_PAD    (3)
#define  SGL_DROPDOWN_OPTION_SPACE  (3)

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
    sgl_draw_rect_t bg_desc = {
        .alpha = dropdown->alpha,
        .color = dropdown->bg_color,
        .border = obj->border,
        .border_alpha = dropdown->alpha,
        .border_color = dropdown->border_color,
        .radius = obj->radius,
        .pixmap = NULL,
    };
    sgl_rect_t bg_coords = obj->coords;
    const int item_height = sgl_font_get_height(dropdown->font) + 2 * SGL_DROPDOWN_OPTION_SPACE;
    const int item_pad = sgl_max(obj->radius, obj->border + SGL_DROPDOWN_OPTION_PAD);

    if (dropdown->option_h == 0) {
        dropdown->option_h = sgl_obj_get_height(obj);
    };
    const int list_h = dropdown->option_h * dropdown->max_visible_item;
    sgl_dropdown_item_t *item = dropdown->head;

    switch (evt->type) {
    case SGL_EVENT_DRAW_MAIN:
        bg_coords.y2 = bg_coords.y1 + dropdown->option_h - 1;
        sgl_draw_rect(surf, &obj->area, &bg_coords, &bg_desc);
        const int text_pos_y = (dropdown->option_h - sgl_font_get_height(dropdown->font) + 1) / 2;

        if (dropdown->is_open) {
            sgl_draw_icon(surf, &obj->area, obj->coords.x2 - dropdown_icon.width - obj->radius, 
                            obj->coords.y1 + (item_height - dropdown_icon.height + 1) / 2 + 2, dropdown->text_color, dropdown->alpha, &dropdown_icon);
        }
        else {
            sgl_draw_icon(surf, &obj->area, obj->coords.x2 - dropdown_icon.width - obj->radius,
                            obj->coords.y1 + (item_height - dropdown_icon.height + 1) / 2, dropdown->text_color, dropdown->alpha, &dropdown_icon);
        }

        for (int i = 0; i < dropdown->item_num && item !=  NULL; i++, item = item->next) {
            if (i == dropdown->item_selected) {
                sgl_draw_string(surf, &obj->area, obj->coords.x1 + item_pad, obj->coords.y1 + text_pos_y, item->text, dropdown->text_color, dropdown->alpha, dropdown->font);
                item = dropdown->head;
                break;
            }
        }

        if (dropdown->is_open) {
            const int16_t text_pos_x1 = obj->coords.x1 + item_pad;
            const int16_t text_pos_x2 = obj->coords.x2 - item_pad;
            const int16_t hline_h = item_height - SGL_DROPDOWN_OPTION_SPACE;

            int item_idx = 0;
            sgl_rect_t select = {
                .x1 = obj->coords.x1,
                .x2 = obj->coords.x2,
            };

            bg_coords.y1 = obj->coords.y1 + dropdown->option_h;
            bg_coords.y2 = bg_coords.y1 + dropdown->max_visible_item * item_height - 1;
            sgl_draw_rect(surf, &obj->area, &bg_coords, &bg_desc);
            
            sgl_area_t bg_area = {
                .x1 = obj->area.x1,
                .x2 = obj->area.x2,
                .y1 = bg_coords.y1,
                .y2 = bg_coords.y2,
            };

            int16_t text_pos_y =  bg_coords.y1 + SGL_DROPDOWN_OPTION_SPACE;
            text_pos_y += dropdown->pos_y;
            sgl_draw_fill_hline(surf, &bg_area, text_pos_y - SGL_DROPDOWN_OPTION_SPACE, text_pos_x1, text_pos_x2, 1,
                                                dropdown->text_color, dropdown->alpha);

            while (item != NULL) {
                if (text_pos_y >= bg_area.y2) {
                    break;
                }

                if (item_idx == dropdown->item_selected) {
                    select.y1 = text_pos_y - SGL_DROPDOWN_OPTION_SPACE;
                    select.y2 = select.y1 + item_height;

                    if ((select.y1 >= (bg_coords.y1 + obj->radius)) && (select.y2 <= (bg_coords.y2 - obj->radius))) {
                        sgl_draw_fill_rect(surf, &bg_area, &select, 0, dropdown->item_selected_color, dropdown->alpha / 2);
                    }
                    else if (select.y1 <= (bg_coords.y1 + obj->radius)) {
                        sgl_area_t select_coords = select;
                        select_coords.y1 = bg_coords.y1;
                        select_coords.y2 = select.y1 + item_height + obj->radius + 1;
                        sgl_draw_fill_rect(surf, &select, &select_coords, obj->radius, dropdown->item_selected_color, dropdown->alpha / 2);
                    }
                    else if (select.y2 >= (bg_coords.y2 - obj->radius)) {
                        sgl_area_t select_coords = select;
                        select_coords.y1 = select.y1 - item_height - obj->radius - 1;
                        select_coords.y2 = bg_coords.y2;
                        sgl_draw_fill_rect(surf, &select, &select_coords, obj->radius, dropdown->item_selected_color, dropdown->alpha / 2);
                    }
                }

                sgl_draw_string(surf, &bg_area, text_pos_x1, text_pos_y, item->text, dropdown->text_color, dropdown->alpha, dropdown->font);

                sgl_draw_fill_hline(surf, &bg_area, text_pos_y + hline_h, text_pos_x1, text_pos_x2, 1,
                                                dropdown->text_color, dropdown->alpha);

                text_pos_y += item_height;
                item = item->next;
                item_idx ++;
            }
        }
        break;
    
    case SGL_EVENT_MOVE_UP:
        if (dropdown->is_open) {
            if((dropdown->pos_y + (dropdown->item_num) * item_height) >= (list_h - item_height / 2)) {
                dropdown->pos_y -= evt->distance;
            }
            sgl_obj_set_dirty(obj);
        }
        break;

    case SGL_EVENT_MOVE_DOWN:
        if (dropdown->is_open) {
            if(dropdown->pos_y < item_height / 2) {
                dropdown->pos_y += evt->distance;
            }
            sgl_obj_set_dirty(obj);
        }
        break;

    case SGL_EVENT_KEY_ENTER:
    case SGL_EVENT_CLICKED:
        if (dropdown->is_open) {
            dropdown->is_open = false;
            obj->coords.y2 =  obj->coords.y1 + dropdown->option_h - 1;
            if (evt->pos.y > obj->coords.y2) {
                dropdown->item_selected = (evt->pos.y - obj->coords.y2 - dropdown->pos_y) / item_height;
            }
        }
        else {
            dropdown->is_open = true;
            obj->coords.y2 = obj->coords.y1 + dropdown->option_h + dropdown->max_visible_item * item_height - 1;
        }

        sgl_obj_set_dirty(obj);
        break;

    case SGL_EVENT_RELEASED:
        if (dropdown->pos_y > 0) {
            dropdown->pos_y = 0;
            sgl_obj_set_dirty(obj);
        }

        if((dropdown->pos_y + dropdown->item_num * item_height) < list_h) {
            dropdown->pos_y = list_h - dropdown->item_num * item_height;
            sgl_obj_set_dirty(obj);
        }
        break;

    case SGL_EVENT_DESTROYED:
        while (item != NULL) {
            sgl_dropdown_item_t *next = item->next;
            sgl_free(item);
            item = next;
        }
        break;

    default: break;
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
    sgl_obj_set_border_width(obj, 1);
    sgl_obj_set_clickable(obj);
    sgl_obj_set_movable(obj);

    dropdown->alpha = SGL_THEME_ALPHA;
    dropdown->bg_color = SGL_THEME_COLOR;
    dropdown->border_color = SGL_THEME_BORDER_COLOR;
    dropdown->item_selected_color = SGL_THEME_BG_COLOR;
    dropdown->item_num = 0;
    dropdown->is_open = false;
    dropdown->font = sgl_get_system_font();
    dropdown->head = NULL;
    dropdown->tail = NULL;
    dropdown->pos_y = 0;
    dropdown->item_selected = -1;
    dropdown->max_visible_item = 5;
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
    dropdown->bg_color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set dropdown object's border width
 * @param obj dropdown object
 * @param width border width
 */
void sgl_dropdown_set_border_width(sgl_obj_t *obj, uint8_t width)
{
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
    dropdown->border_color = color;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set the selected color of dropdown
 * @param obj dropdown object
 * @param color selected option color
 */
void sgl_dropdown_set_selected_color(sgl_obj_t *obj, sgl_color_t color)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    dropdown->item_selected_color = color;
}

/**
 * @brief set dropdown object's radius
 * @param obj dropdown object
 * @param radius radius
 */
void sgl_dropdown_set_radius(sgl_obj_t *obj, uint8_t radius)
{
    sgl_obj_set_radius(obj, radius);
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
    dropdown->alpha = alpha;
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
 * @brief get dropdown object's selected index
 * @param obj dropdown object
 * @return selected index
 */
int sgl_dropdown_get_selected_index(sgl_obj_t *obj)
{
    sgl_dropdown_t *dropdown = sgl_container_of(obj, sgl_dropdown_t, obj);
    return dropdown->item_selected;
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
    sgl_dropdown_item_t *tail = dropdown->head;
    sgl_dropdown_item_t *add = sgl_malloc(sizeof(sgl_dropdown_item_t));

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

    dropdown->item_num ++;
    add->text = text;
    add->next = NULL;

    if (dropdown->item_selected == -1) {
        dropdown->item_selected = 0;
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
    sgl_dropdown_item_t *curr = dropdown->head;
    sgl_dropdown_item_t *temp = dropdown->head;
    sgl_dropdown_item_t *prev = NULL;
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
    dropdown->item_num --;
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
    sgl_dropdown_item_t *curr = dropdown->head;
    sgl_dropdown_item_t *prev = NULL;

    if (obj == NULL || index < 0 || index >= dropdown->item_num) {
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
    dropdown->item_num--;
}
