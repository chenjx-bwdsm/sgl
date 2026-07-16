/* source/widgets/sgl_img.c
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
#include <sgl_theme.h>
#include <sgl_cfgfix.h>
#include <string.h>
#include "sgl_img.h"

static inline void img_rle_init(sgl_img_t *img)
{
    SGL_ASSERT(img != NULL);
    img->index = 0;
    img->remainder = 0;
}

static inline void rle_decompress_line(sgl_img_t *img, sgl_area_t *coords, sgl_area_t *area, sgl_color_t *out)
{
    uint8_t tmp_buf[8];
    uint8_t* read_ptr = NULL;
    uint8_t* start_ptr = (uint8_t*)img->pixmap[img->pixmap_idx].bitmap.array;
    uintptr_t start_addr = img->pixmap[img->pixmap_idx].bitmap.addr;
    uint8_t format = img->pixmap->format;
    uint32_t pix_value;

    for (int i = coords->x1; i <= coords->x2; i++) {
        if (img->remainder == 0) {
            if (img->read != NULL) {
                read_ptr = tmp_buf;
                img->read(start_addr + img->index, tmp_buf, sizeof(tmp_buf));
            }
            else {
                read_ptr = start_ptr + img->index;
            }

            img->index ++;
            img->remainder = read_ptr[0];

            switch (format) {
            case SGL_PIXMAP_FMT_RLE_RGB332:
                pix_value = read_ptr[1];
                img->color = sgl_rgb332_to_color(pix_value);
                img->pix_alpha = SGL_ALPHA_MAX;
                img->index ++;
                break;
            case SGL_PIXMAP_FMT_RLE_ARGB2222:
                pix_value = read_ptr[1];
                img->color = sgl_rgb222_to_color(pix_value);
                img->pix_alpha = sgl_opa2_table[pix_value >> 6];
                img->index ++;
                break;
            case SGL_PIXMAP_FMT_RLE_RGB565:
                pix_value = read_ptr[1] | (read_ptr[2] << 8);
                img->color = sgl_rgb565_to_color(pix_value);
                img->pix_alpha = SGL_ALPHA_MAX;
                img->index += 2;
                break;
            case SGL_PIXMAP_FMT_RLE_ARGB4444:
                pix_value = read_ptr[1] | (read_ptr[2] << 8);
                img->color = sgl_rgb444_to_color(pix_value);
                img->pix_alpha = sgl_opa4_table[pix_value >> 12];
                img->index += 2;
                break;
            case SGL_PIXMAP_FMT_RLE_RGB888:
                pix_value = read_ptr[1] | (read_ptr[2] << 8) | (read_ptr[3] << 16);
                img->color = sgl_rgb888_to_color(pix_value);
                img->pix_alpha = SGL_ALPHA_MAX;
                img->index += 3;
                break;
            case SGL_PIXMAP_FMT_RLE_ARGB8888:
                pix_value = read_ptr[1] | (read_ptr[2] << 8) | (read_ptr[3] << 16);
                img->color = sgl_rgb888_to_color(pix_value);
                img->pix_alpha = read_ptr[4];
                img->index += 4;
                break;
            default:
                break;
            }
        }

        if (out != NULL && i >= area->x1 && i <= area->x2) {
            img->color = (img->pix_alpha == SGL_ALPHA_MAX ? img->color : sgl_color_mixer(img->color, *out, img->pix_alpha));
            *out = (img->alpha == SGL_ALPHA_MAX ? img->color : sgl_color_mixer(img->color, *out, img->alpha));
            out ++;
        }
        img->remainder --;
    };
}


static void sgl_img_construct_cb(sgl_surf_t *surf, sgl_obj_t* obj, sgl_event_t *evt)
{
    sgl_area_t clip = SGL_AREA_INVALID;
    sgl_img_t *img = sgl_container_of(obj, sgl_img_t, obj);
    const sgl_pixmap_t *pixmap = &img->pixmap[img->pixmap_idx];
    uintptr_t read_addr = pixmap->bitmap.addr;
    uint8_t pix_byte = sgl_pixmal_get_pixel_bytes(pixmap);
    sgl_color_t tmp_color, *buf = NULL, *blend = NULL;
    uint8_t *pixmap_buf = (uint8_t*)pixmap->bitmap.array;
    size_t offset = 0;

    sgl_area_t area = {
        .x1 = obj->coords.x1,
        .y1 = obj->coords.y1,
        .x2 = obj->coords.x1 + pixmap->width - 1,
        .y2 = obj->coords.y1 + pixmap->height - 1,
    };

    if(evt->type == SGL_EVENT_DRAW_MAIN) {
        if (!sgl_surf_clip(surf, &obj->area, &clip)) {
            return;
        }

        if (img->read != NULL) {
            /* Calculate buffer lines: 0 means use full clip height, otherwise use configured value capped at clip height */
            uint16_t buf_lines = SGL_IMG_BUFFER_SIZE;
            if (buf_lines == 0) {
                buf_lines = (uint16_t)(clip.y2 - clip.y1 + 1);
            }
            else if (buf_lines > (uint16_t)(clip.y2 - clip.y1 + 1)) {
                buf_lines = (uint16_t)(clip.y2 - clip.y1 + 1);
            }
            img->buffer_lines = buf_lines;

            if (img->flash_buffer == NULL) {
                img->flash_buffer = (uint8_t*)sgl_malloc(pix_byte * (clip.x2 - clip.x1 + 1) * buf_lines);
            }
            pixmap_buf = img->flash_buffer;
        }
        else {
            /* No external read: use 1-line buffer mode, read directly from memory */
            img->buffer_lines = 1;
        }

        if (pixmap->format < SGL_PIXMAP_FMT_RLE_RGB332) {
            buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, clip.y1 - surf->y1);

            uint16_t row_width_bytes = pix_byte * (uint16_t)(clip.x2 - clip.x1 + 1);
            uint16_t buf_lines = img->buffer_lines;
            uint16_t lines_in_buf = 0;

/* Y-loop header: refill buffer, compute blend/offset for current row.
 * Usage: DRAW_Y_HEAD() followed by a for(x...) pixel loop, then DRAW_Y_TAIL() */
#define DRAW_Y_HEAD()                                                        \
            for (int y = clip.y1; y <= clip.y2; y++) {                       \
                if (lines_in_buf == 0) {                                     \
                    uint16_t _ltr = buf_lines;                               \
                    if ((uint16_t)(clip.y2 - y + 1) < _ltr)                  \
                        _ltr = (uint16_t)(clip.y2 - y + 1);                  \
                    offset = (((y - area.y1) * pixmap->width)                \
                        + (clip.x1 - area.x1)) * pix_byte;                   \
                    if (img->read != NULL) {                                 \
                        img->read(read_addr + offset, pixmap_buf,            \
                            row_width_bytes * _ltr);                         \
                        img->buffer_line = (uint16_t)(y - clip.y1);          \
                    } else {                                                 \
                        pixmap_buf = (uint8_t*)pixmap->bitmap.array + offset;\
                    }                                                        \
                    lines_in_buf = _ltr;                                     \
                }                                                            \
                blend = buf;                                                 \
                offset = (img->read != NULL)                                 \
                    ? (uint16_t)(y - clip.y1 - img->buffer_line) * row_width_bytes : 0

#define DRAW_Y_TAIL()                                                        \
                buf += surf->w;                                              \
                lines_in_buf --;                                             \
            }

            switch (pixmap->format) {
            case SGL_PIXMAP_FMT_RGB332:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        tmp_color = sgl_rgb332_to_color(pixmap_buf[offset]);
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 1; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            case SGL_PIXMAP_FMT_RGB565:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        tmp_color = sgl_rgb565_to_color(pixmap_buf[offset] | (pixmap_buf[offset + 1] << 8));
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 2; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            case SGL_PIXMAP_FMT_ARGB2222:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        uint8_t _v = pixmap_buf[offset];
                        tmp_color = sgl_rgb222_to_color(_v);
                        tmp_color = sgl_color_mixer(tmp_color, *blend, sgl_opa2_table[_v >> 6]);
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 1; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            case SGL_PIXMAP_FMT_ARGB4444:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        uint16_t _v = pixmap_buf[offset] | (pixmap_buf[offset + 1] << 8);
                        tmp_color = sgl_rgb444_to_color(_v);
                        tmp_color = sgl_color_mixer(tmp_color, *blend, sgl_opa4_table[_v >> 12]);
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 2; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            case SGL_PIXMAP_FMT_RGB888:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        tmp_color = sgl_rgb888_to_color(
                            pixmap_buf[offset] | (pixmap_buf[offset + 1] << 8) | (pixmap_buf[offset + 2] << 16));
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 3; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            case SGL_PIXMAP_FMT_ARGB8888:
                DRAW_Y_HEAD();
                    for (int x = clip.x1; x <= clip.x2; x++) {
                        tmp_color = sgl_rgb888_to_color(
                            pixmap_buf[offset] | (pixmap_buf[offset + 1] << 8) | (pixmap_buf[offset + 2] << 16));
                        tmp_color = sgl_color_mixer(tmp_color, *blend, pixmap_buf[offset + 3]);
                        *blend = (img->alpha == SGL_ALPHA_MAX) ? tmp_color
                            : sgl_color_mixer(tmp_color, *blend, img->alpha);
                        offset += 4; blend ++;
                    }
                DRAW_Y_TAIL();
                break;
            default:
                break;
            }

#undef DRAW_Y_HEAD
#undef DRAW_Y_TAIL
        }
        else {
            /* RLE pixmap support */
            if (clip.y1 == surf->dirty->y1 || clip.y1 == obj->area.y1) {
                img->index = 0;
                img->remainder = 0;
                for (int y = area.y1; y < clip.y1; y++) {
                    rle_decompress_line(img, &area, &clip, NULL);
                }
            }

            buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, (clip.y1 - surf->y1));

            for (int y = clip.y1; y <= clip.y2; y++) {
                rle_decompress_line(img, &area, &clip, buf);
                buf += surf->w;
            }
        }

        if (img->pixmap_auto && sgl_obj_draw_complete(obj, surf, &clip)) {
            uint32_t pixmap_idx = img->pixmap_idx + 1;
            img->pixmap_idx = pixmap_idx >= img->pixmap_num ? 0 : pixmap_idx;
            sgl_obj_set_dirty(obj);
        }
    }
    else if (evt->type == SGL_EVENT_DESTROYED) {
        if (img->flash_buffer != NULL) {
            sgl_free(img->flash_buffer);
        }
    }
}

/**
 * @brief create an img object
 * @param parent parent of the img
 * @return img object
 */
sgl_obj_t* sgl_img_create(sgl_obj_t* parent)
{
    sgl_img_t *img = sgl_malloc(sizeof(sgl_img_t));
    if(img == NULL) {
        SGL_LOG_ERROR("sgl_img_create: malloc failed");
        return NULL;
    }

    /* set object all member to zero */
    memset(img, 0, sizeof(sgl_img_t));

    sgl_obj_t *obj = &img->obj;
    sgl_obj_init(&img->obj, parent);
    obj->construct_fn = sgl_img_construct_cb;

    img->alpha = SGL_ALPHA_MAX;
    img->pixmap_idx = 0;
    img->pixmap_num = 1;
    img->pixmap_auto = 0;

    return obj;
}

/**
 * @brief set img pixmap
 * @param obj img object
 * @param pixmap img pixmap
 * @return none
 */
void sgl_img_set_pixmap(sgl_obj_t *obj, const sgl_pixmap_t *pixmap)
{
    SGL_ASSERT(obj != NULL);
    ((sgl_img_t*)obj)->pixmap = pixmap;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set img read operation
 * @param obj img object
 * @param read img read operation
 * @return none
 */
void sgl_img_set_read_ops(sgl_obj_t *obj, void (*read)(const size_t addr, uint8_t *out, uint32_t len_bytes))
{
    SGL_ASSERT(obj != NULL);
    ((sgl_img_t*)obj)->read = read;
}

/**
 * @brief set img alpha
 * @param obj img object
 * @param alpha img alpha
 * @return none
 */
void sgl_img_set_alpha(sgl_obj_t *obj, uint8_t alpha)
{
    SGL_ASSERT(obj != NULL);
    ((sgl_img_t*)obj)->alpha = alpha;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set img pixmap number
 * @param obj img object
 * @param num img pixmap number
 * @param auto_refresh img pixmap auto refresh
 * @return none
 * @note if auto_refresh is true, the img will refresh automatically after pixmap flush conplete
 * @warning the num max is 255
 */
void sgl_img_set_pixmap_num(sgl_obj_t *obj, uint8_t num, bool auto_refresh)
{
    SGL_ASSERT(obj != NULL);
    ((sgl_img_t*)obj)->pixmap_num = num;
    ((sgl_img_t*)obj)->pixmap_auto = (uint8_t)auto_refresh;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set img next pixmap
 * @param obj img object
 * @return none
 */
void sgl_img_set_pixmap_next(sgl_obj_t *obj)
{
    SGL_ASSERT(obj != NULL);
    sgl_img_t *img = sgl_container_of(obj, sgl_img_t, obj);
    uint32_t pixmap_idx = img->pixmap_idx + 1;
    img->pixmap_idx = pixmap_idx >= img->pixmap_num ? 0 : pixmap_idx;
    sgl_obj_set_dirty(obj);
}

/**
 * @brief set img pixmap current index
 * @param obj img object
 * @param index img pixmap index
 * @return none
 */
void sgl_img_set_pixmap_index(sgl_obj_t *obj, uint8_t index)
{
    SGL_ASSERT(obj != NULL);
    sgl_img_t *img = sgl_container_of(obj, sgl_img_t, obj);
    img->pixmap_idx = sgl_min(index, img->pixmap_num - 1);
    sgl_obj_set_dirty(obj);
}
