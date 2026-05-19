/* source/core/sgl_misc.c
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

#include <sgl.h>

#if (CONFIG_SGL_BOOT_LOGO)

typedef struct sgl_logo {
    sgl_obj_t       obj;
    uint8_t         progress;
    uint8_t         alpha;
}sgl_logo_t;

#define SGL_LOGO_W    (26)
#define SGL_LOGO_H    (14)

const uint8_t sgl_logo_s[][4] = {
    {0, 0, 8, 2},
    {0, 2, 2, 8},
    {2, 6, 8, 8},
    {6, 8, 8, 14},
    {0, 12, 6, 14},
};

const uint8_t sgl_logo_g[][4] = {
    {9,  0,  17, 2},
    {9,  2,  11, 14},
    {11, 12, 17, 14},
    {15, 6,  17, 12},
    {13, 6,  15, 8},
};

const uint8_t sgl_logo_l[][4] = {
    {18, 0, 20, 14},
    {20, 12, 26, 14},
};

static void sgl_logo_construct_cb(sgl_surf_t *surf, sgl_obj_t* obj, sgl_event_t *evt)
{
    sgl_logo_t *logo = (sgl_logo_t*)obj;
    int16_t w = obj->area.x2 - obj->area.x1 + 1;
    int16_t h = obj->area.y2 - obj->area.y1 + 1;

    const float scale_x = w / SGL_LOGO_W;
    const float scale_y = h / SGL_LOGO_H;
    sgl_rect_t rect;

    if(evt->type == SGL_EVENT_DRAW_MAIN) {
        sgl_area_t clip;

        if (!sgl_surf_clip(surf, &obj->area, &clip)) {
            return;
        }

        for (uint32_t i = 0; i < SGL_ARRAY_SIZE(sgl_logo_s); i++) {
            rect.x1 = sgl_logo_s[i][0] * scale_x + obj->area.x1;
            rect.y1 = sgl_logo_s[i][1] * scale_y + obj->area.y1;
            rect.x2 = sgl_logo_s[i][2] * scale_x + obj->area.x1 - 1;
            rect.y2 = sgl_logo_s[i][3] * scale_y + obj->area.y1 - 1;

            sgl_draw_fill_rect(surf, &clip, &rect, obj->radius, SGL_COLOR_RED, logo->alpha);
        }

        for (uint32_t i = 0; i < SGL_ARRAY_SIZE(sgl_logo_g); i++) {
            rect.x1 = sgl_logo_g[i][0] * scale_x + obj->area.x1;
            rect.y1 = sgl_logo_g[i][1] * scale_y + obj->area.y1;
            rect.x2 = sgl_logo_g[i][2] * scale_x + obj->area.x1 - 1;
            rect.y2 = sgl_logo_g[i][3] * scale_y + obj->area.y1 - 1;

            sgl_draw_fill_rect(surf, &clip, &rect, obj->radius, SGL_COLOR_GREEN, logo->alpha);
        }

        for (uint32_t i = 0; i < SGL_ARRAY_SIZE(sgl_logo_l); i++) {
            rect.x1 = sgl_logo_l[i][0] * scale_x + obj->area.x1;
            rect.y1 = sgl_logo_l[i][1] * scale_y + obj->area.y1;
            rect.x2 = sgl_logo_l[i][2] * scale_x + obj->area.x1 - 1;
            rect.y2 = sgl_logo_l[i][3] * scale_y + obj->area.y1 - 1;

            sgl_draw_fill_rect(surf, &clip, &rect, obj->radius, SGL_COLOR_BLUE, logo->alpha);
        }
    }
}

sgl_obj_t* sgl_logo_create(sgl_obj_t* parent)
{
    sgl_logo_t *logo = sgl_malloc(sizeof(sgl_logo_t));
    if (logo == NULL) {
        SGL_LOG_ERROR("sgl_logo_create: malloc failed");
        return NULL;
    }

    /* set object all member to zero */
    memset(logo, 0, sizeof(sgl_logo_t));

    sgl_obj_t *obj = &logo->obj;
    sgl_obj_init(&logo->obj, parent);
    obj->construct_fn = sgl_logo_construct_cb;
    logo->alpha = SGL_ALPHA_MAX;
    sgl_obj_set_border_width(obj, 0);
    return obj;
}

void sgl_logo_set_alpha(sgl_obj_t* obj, uint8_t alpha)
{
    sgl_logo_t *logo = (sgl_logo_t*)obj;
    logo->alpha = alpha;
    sgl_obj_set_dirty(obj);
}

void sgl_logo_anim(sgl_anim_t *anim, int32_t value)
{
    sgl_obj_t *logo = (sgl_obj_t*)anim->data;
    sgl_logo_set_alpha(logo, value);
}

/**
 * @brief to show the sgl logo after sgl init
 * @param none
 * @return none
 * @note: you can call this function in your main function to show the sgl logo
 */
void sgl_boot_logo(void)
{
    sgl_obj_t *logo = sgl_logo_create(NULL);
    sgl_obj_set_size(logo, SGL_SCREEN_WIDTH / 3, SGL_SCREEN_HEIGHT / 3);
    sgl_obj_set_pos_align(logo, SGL_ALIGN_CENTER);
    sgl_obj_set_radius(logo, 0);

    sgl_anim_t *anim = sgl_anim_create();
    sgl_anim_set_data(anim, logo);
    sgl_anim_set_act_duration(anim, 1000);
    sgl_anim_set_start_value(anim, SGL_ALPHA_MAX);
    sgl_anim_set_end_value(anim, SGL_ALPHA_MIN);
    sgl_anim_set_path(anim, sgl_logo_anim, SGL_ANIM_PATH_LINEAR);
    sgl_anim_start(anim, SGL_ANIM_REPEAT_ONCE);

    while (!sgl_anim_is_finished(anim)) {
        sgl_task_handler();
    }

    sgl_anim_delete(anim);
    sgl_obj_delete_sync(logo);
}

#endif // !CONFIG_SGL_BOOT_LOGO


#if (CONFIG_SGL_MONITOR_TRACE)
static const uint8_t font_bitmap[] = {
    /* U+0025 "%" */
    0x2c, 0xe7, 0x00, 0x42, 0x98, 0x0f, 0x14, 0xe3,
    0x88, 0x1f, 0x2c, 0x20, 0x1b, 0xd6, 0x00, 0x00,
    0x00, 0x00, 0x5d, 0xb0, 0x00, 0xc2, 0xe2, 0x97,
    0x0b, 0x71, 0xe0, 0x6a, 0x6b, 0x00, 0xf1, 0x97,
    0x01, 0x00, 0x6e, 0xc1,

    /* U+002E "." */
    0x07, 0x70, 0x2f, 0xf2, 0x0c, 0xc0,

    /* U+0030 "0" */
    0x00, 0x8e, 0xe8, 0x00, 0x07, 0xe4, 0x4e, 0x70,
    0x0d, 0x70, 0x07, 0xd0, 0x0f, 0x49, 0x93, 0xf0,
    0x1f, 0x3a, 0xa2, 0xf1, 0x0f, 0x40, 0x04, 0xf0,
    0x0d, 0x70, 0x07, 0xd0, 0x06, 0xe5, 0x4e, 0x60,
    0x00, 0x7e, 0xe7, 0x00,

    /* U+0031 "1" */
    0x17, 0xcf, 0x00, 0x02, 0x9c, 0xf0, 0x00, 0x00,
    0x6f, 0x00, 0x00, 0x06, 0xf0, 0x00, 0x00, 0x6f,
    0x00, 0x00, 0x06, 0xf0, 0x00, 0x00, 0x6f, 0x00,
    0x02, 0x38, 0xf3, 0x30, 0xcf, 0xff, 0xff, 0x10,

    /* U+0032 "2" */
    0x03, 0xbe, 0xd6, 0x00, 0x0d, 0x84, 0x9f, 0x40,
    0x00, 0x00, 0x0e, 0x80, 0x00, 0x00, 0x0e, 0x60,
    0x00, 0x00, 0x7e, 0x00, 0x00, 0x04, 0xf4, 0x00,
    0x00, 0x5f, 0x50, 0x00, 0x06, 0xf8, 0x33, 0x30,
    0x1f, 0xff, 0xff, 0xf0,

    /* U+0033 "3" */
    0x03, 0xbe, 0xe8, 0x00, 0xa8, 0x37, 0xf7, 0x00,
    0x00, 0x0d, 0x90, 0x00, 0x05, 0xf3, 0x00, 0x5f,
    0xf5, 0x00, 0x00, 0x36, 0xe7, 0x00, 0x00, 0x08,
    0xe1, 0xd7, 0x46, 0xeb, 0x05, 0xce, 0xea, 0x10,

    /* U+0034 "4" */
    0x00, 0x00, 0xdf, 0x10, 0x00, 0x0a, 0xcf, 0x10,
    0x00, 0x7d, 0x4f, 0x10, 0x03, 0xf2, 0x3f, 0x10,
    0x1e, 0x50, 0x3f, 0x10, 0x7f, 0xff, 0xff, 0xf5,
    0x12, 0x22, 0x5f, 0x40, 0x00, 0x00, 0x3f, 0x10,
    0x00, 0x00, 0x3f, 0x10,

    /* U+0035 "5" */
    0x06, 0xff, 0xff, 0x90, 0x07, 0xe3, 0x33, 0x20,
    0x08, 0xc0, 0x00, 0x00, 0x09, 0xed, 0xe9, 0x10,
    0x03, 0x63, 0x6e, 0xa0, 0x00, 0x00, 0x07, 0xf0,
    0x00, 0x00, 0x08, 0xe0, 0x1e, 0x84, 0x8f, 0x80,
    0x05, 0xbe, 0xe8, 0x00,

    /* U+0036 "6" */
    0x00, 0x3c, 0xfd, 0x60, 0x02, 0xf8, 0x47, 0x60,
    0x0a, 0xa0, 0x00, 0x00, 0x0e, 0x7a, 0xeb, 0x20,
    0x0f, 0xe5, 0x4c, 0xd0, 0x0f, 0x60, 0x04, 0xf1,
    0x0c, 0x90, 0x05, 0xf1, 0x05, 0xf6, 0x4c, 0xb0,
    0x00, 0x6d, 0xfa, 0x10,

    /* U+0037 "7" */
    0x1f, 0xff, 0xff, 0xf2, 0x03, 0x33, 0x3c, 0xb0,
    0x00, 0x00, 0x4e, 0x10, 0x00, 0x00, 0xd6, 0x00,
    0x00, 0x05, 0xf0, 0x00, 0x00, 0x0a, 0xb0, 0x00,
    0x00, 0x0d, 0x80, 0x00, 0x00, 0x0f, 0x60, 0x00,
    0x00, 0x1f, 0x50, 0x00,

    /* U+0038 "8" */
    0x00, 0x9e, 0xea, 0x00, 0x07, 0xe3, 0x3d, 0x80,
    0x09, 0xa0, 0x08, 0xa0, 0x03, 0xe5, 0x0c, 0x40,
    0x00, 0xbe, 0xfc, 0x00, 0x0b, 0x80, 0x3c, 0xb0,
    0x1f, 0x30, 0x04, 0xf1, 0x0e, 0xb3, 0x3b, 0xe0,
    0x02, 0xbe, 0xeb, 0x20,

    /* U+0039 "9" */
    0x02, 0xbf, 0xd6, 0x00, 0x0d, 0xb3, 0x5f, 0x50,
    0x1f, 0x40, 0x07, 0xc0, 0x0f, 0x90, 0x2b, 0xf0,
    0x05, 0xff, 0xd8, 0xf0, 0x00, 0x01, 0x07, 0xe0,
    0x00, 0x00, 0x0c, 0xa0, 0x09, 0x74, 0xaf, 0x20,
    0x05, 0xcf, 0xc3, 0x00,

    /* U+003A ":" */
    0x07, 0x70, 0x2f, 0xf2, 0x0c, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x70, 0x2f, 0xf2, 0x0c, 0xc0,

    /* U+0045 "E" */
    0x9f, 0xff, 0xff, 0x09, 0xe3, 0x33, 0x30, 0x9d,
    0x00, 0x00, 0x09, 0xd0, 0x00, 0x00, 0x9f, 0xff,
    0xf5, 0x09, 0xe3, 0x33, 0x10, 0x9d, 0x00, 0x00,
    0x09, 0xe3, 0x33, 0x30, 0x9f, 0xff, 0xff, 0x10,

    /* U+0046 "F" */
    0x5f, 0xff, 0xff, 0x25, 0xf4, 0x33, 0x30, 0x5f,
    0x10, 0x00, 0x05, 0xf1, 0x00, 0x00, 0x5f, 0xff,
    0xf8, 0x05, 0xf4, 0x33, 0x10, 0x5f, 0x10, 0x00,
    0x05, 0xf1, 0x00, 0x00, 0x5f, 0x10, 0x00, 0x00,

    /* U+004D "M" */
    0x1f, 0xa0, 0x0a, 0xf1, 0x1f, 0xe0, 0x0e, 0xf1,
    0x1f, 0xc4, 0x4c, 0xf1, 0x1f, 0x98, 0x98, 0xf1,
    0x1f, 0x4d, 0xd4, 0xf1, 0x1f, 0x2d, 0xc2, 0xf1,
    0x1f, 0x25, 0x52, 0xf1, 0x1f, 0x20, 0x02, 0xf1,
    0x1f, 0x20, 0x02, 0xf1,

    /* U+0050 "P" */
    0xcf, 0xff, 0xc4, 0x0c, 0xb3, 0x3a, 0xf1, 0xca,
    0x00, 0x2f, 0x4c, 0xa0, 0x18, 0xf1, 0xcf, 0xff,
    0xe5, 0x0c, 0xb2, 0x20, 0x00, 0xca, 0x00, 0x00,
    0x0c, 0xa0, 0x00, 0x00, 0xca, 0x00, 0x00, 0x00,

    /* U+0053 "S" */
    0x00, 0x9e, 0xeb, 0x20, 0x08, 0xf7, 0x5a, 0xa0,
    0x0b, 0xc0, 0x00, 0x00, 0x06, 0xfa, 0x30, 0x00,
    0x00, 0x4d, 0xfb, 0x20, 0x00, 0x00, 0x4d, 0xe0,
    0x01, 0x00, 0x05, 0xf2, 0x0e, 0xb6, 0x6c, 0xe0,
    0x02, 0xae, 0xeb, 0x20
};


static const sgl_font_table_t font_table[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 36, .adv_w = 128, .box_w = 4, .box_h = 3, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 42, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 146, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 214, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 250, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 286, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 322, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 358, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 394, .adv_w = 128, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 410, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 442, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 474, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 542, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0}
};

static const uint16_t unicode_list_0[] = {
    0x00, 0x09, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x20, 0x21, 0x28,
    0x2b, 0x2e
};

static const sgl_font_unicode_t font_unicode[] =
{
    { .offset = 0x25, .len = 18, .list = unicode_list_0, .tab_offset = 1, }
};

const sgl_font_t monitor_font = {
    .bitmap = font_bitmap,
    .table = font_table,
    .font_table_size = SGL_ARRAY_SIZE(font_table),
    .font_height = 9,
    .base_line = 0,
    .bpp = 4,
    .compress = 0,
    .unicode = font_unicode,
    .unicode_num = SGL_ARRAY_SIZE(font_unicode),
};

void sgl_monitor_trace(sgl_surf_t *surf)
{
    static char fps_str[16] = {0};
    static char mem_str[16] = {0};
    static sgl_obj_t *monitor = NULL;
    static sgl_obj_t *fps = NULL;
    static sgl_obj_t *mem = NULL;
    sgl_obj_t *child = NULL;
    static uint32_t fps_count = 0, last_tick = 0, tick = 0;
    uint32_t cur_tick = sgl_tick_get();
    sgl_event_t evt = {0};

    if (monitor) {
        if (tick != sgl_last_tick_get()) {
            tick = sgl_last_tick_get();
            fps_count ++;
        }

        if ((cur_tick - last_tick) >= 500) {
            sgl_snprintf(fps_str, sizeof(fps_str), "FPS:%d", fps_count * 2);
            sgl_snprintf(mem_str, sizeof(mem_str), "MEM:%d.%d%", sgl_mm_get_monitor().used_rate >> 8, sgl_mm_get_monitor().used_rate & 0xff);
            fps_count = 0;
            last_tick = cur_tick;
        }

#if (CONFIG_SGL_FBDEV_RUNTIME_ROTATION)
        if ((monitor->coords.y2 + 1) == SGL_SCREEN_WIDTH || (monitor->coords.x2 + 1) == SGL_SCREEN_HEIGHT) {
            sgl_obj_delete(monitor);
            monitor = NULL;
            return;
        } 
#endif

        /* update monitor page */
        evt.type = SGL_EVENT_DRAW_MAIN;
        if (sgl_surf_area_is_overlap(surf, &monitor->area)) {
            monitor->construct_fn(surf, monitor, &evt);
        }

        /* update all child of monitor page */
        sgl_obj_for_each_child(child, monitor) {
            if (sgl_surf_area_is_overlap(surf, &child->area)) {
                child->construct_fn(surf, child, &evt);
            }
        }
    }
    else {
        monitor = sgl_obj_create(NULL);
        sgl_obj_set_pos(monitor, SGL_MONITOR_COORDS_X, SGL_MONITOR_COORDS_Y);
        sgl_obj_set_size(monitor, SGL_MONITOR_COORDS_WIDTH, SGL_MONITOR_COORDS_WIDTH);
        monitor->area = monitor->coords;
        sgl_page_set_color(monitor, SGL_MONITOR_COLOR);
        sgl_page_set_alpha(monitor, SGL_MONITOR_ALPHA);

        fps = sgl_label_create(monitor);
        sgl_obj_set_pos(fps, 0, 0);
        sgl_obj_set_size(fps, SGL_MONITOR_COORDS_WIDTH, SGL_MONITOR_COORDS_HEIGHT / 2);
        fps->area = monitor->coords;
        sgl_label_set_font(fps, &monitor_font);
        sgl_label_set_text_align(fps, SGL_ALIGN_LEFT_MID);
        sgl_label_set_text_buffer(fps, fps_str, sizeof(fps_str));
        sgl_label_set_text_color(fps, SGL_MONITOR_TEXT_COLOR);

        mem = sgl_label_create(monitor);
        sgl_obj_set_pos(mem, 0, SGL_MONITOR_COORDS_HEIGHT / 2);
        sgl_obj_set_size(mem, SGL_MONITOR_COORDS_WIDTH, SGL_MONITOR_COORDS_HEIGHT / 2);
        mem->area = monitor->coords;
        sgl_label_set_font(mem, &monitor_font);
        sgl_label_set_text_align(mem, SGL_ALIGN_LEFT_MID);
        sgl_label_set_text_buffer(mem, mem_str, sizeof(mem_str));
        sgl_label_set_text_color(mem, SGL_MONITOR_TEXT_COLOR);
    }
}
#endif
