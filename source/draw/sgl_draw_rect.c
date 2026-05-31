/* source/draw/sgl_draw_rect.c
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


/**
 * @brief draw a wireframe rectangle with alpha
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param radius radius of round
 * @param width width of wireframe
 * @param color color of rectangle
 * @param alpha alpha of rectangle
 * @return none
 */
void sgl_draw_wireframe(sgl_surf_t *surf, sgl_area_t *area, sgl_area_t *rect, int16_t radius, int16_t width, sgl_color_t color, uint8_t alpha)
{
    const int16_t x1 = rect->x1;
    const int16_t x2 = rect->x2;
    const int16_t y1 = rect->y1;
    const int16_t y2 = rect->y2;

    if (radius == 0) {
        sgl_draw_fill_hline(surf, area, y1, x1, x2, width, color, alpha);
        sgl_draw_fill_hline(surf, area, y2, x1, x2, width, color, alpha);
        sgl_draw_fill_vline(surf, area, x1, y1 + width - 1, y2 - width + 1, width, color, alpha);
        sgl_draw_fill_vline(surf, area, x2, y1 + width - 1, y2 - width + 1, width, color, alpha);
    }
    else {
        sgl_draw_fill_rect_with_border(surf, area, rect, radius, SGL_COLOR_BLACK, color, width, 0, alpha);
    }
}


/**
 * @brief fill a round rectangle with alpha
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param radius radius of round
 * @param color color of rectangle
 * @param alpha alpha of rectangle
 * @return none
 */
void sgl_draw_fill_rect(sgl_surf_t *surf, sgl_area_t *area, sgl_area_t *rect, int16_t radius, sgl_color_t color, uint8_t alpha)
{
    sgl_area_t clip = SGL_AREA_INVALID;
    sgl_color_t *buf = NULL, *blend = NULL;
    uint8_t solid_alpha = (alpha == SGL_ALPHA_MAX);

    sgl_surf_clip_area_return(surf, area, &clip);
    if (!sgl_area_selfclip(&clip, rect)) return;

    int pixel_count = clip.x2 - clip.x1 + 1;
    buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, clip.y1 - surf->y1);

    if (radius <= 0) {
        for (int y = clip.y1; y <= clip.y2; y++) {
            blend = buf;
            if (solid_alpha) {
                for (int i = 0; i < pixel_count; i++) {
                    blend[i] = color;
                }
            }
            else {
                for (int i = 0; i < pixel_count; i++) {
                    blend[i] = sgl_color_mixer(color, blend[i], alpha);
                }
            }
            buf += surf->w;
        }
        return;
    }

    const int cx1 = rect->x1 + radius;
    const int cx2 = rect->x2 - radius;
    const int cy1 = rect->y1 + radius;
    const int cy2 = rect->y2 - radius;

    const int r2 = sgl_pow2(radius);
    const int r2_max = sgl_pow2(radius + 1);
    const int r2_diff = sgl_max(r2_max - r2, 1);
    const int r2_fix_diff = (SGL_ALPHA_MAX << SGL_FIXED_SHIFT) / r2_diff;

    int cy_tmp, cx_tmp;
    int dy2, real_r2;
    uint8_t edge_alpha;

    for (int y = clip.y1; y <= clip.y2; y++) {
        blend = buf;
        if (y >= cy1 && y <= cy2) {
            pixel_count = clip.x2 - clip.x1 + 1;
            solid_alpha = (alpha == SGL_ALPHA_MAX);

            if (solid_alpha) {
                for (int i = 0; i < pixel_count; i++) {
                    blend[i] = color;
                }
            } else {
                for (int i = 0; i < pixel_count; i++) {
                    blend[i] = sgl_color_mixer(color, blend[i], alpha);
                }
            }
        }
        else {
            cy_tmp = (y < cy1) ? cy1 : cy2;
            dy2 = sgl_pow2(y - cy_tmp);

            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                if (x >= cx1 && x <= cx2) {
                    *blend = (alpha == SGL_ALPHA_MAX ? color : sgl_color_mixer(color, *blend, alpha));
                }
                else {
                    cx_tmp = (x < cx1) ? cx1 : cx2;
                    real_r2 = sgl_pow2(x - cx_tmp) + dy2;

                    if (real_r2 >= r2_max) {
                        if(x > cx_tmp) break; else continue;
                    }
                    else if (real_r2 >= r2) {
                        edge_alpha = ((r2_max - real_r2) * r2_fix_diff) >> SGL_FIXED_SHIFT;
                        *blend = (alpha == SGL_ALPHA_MAX ? sgl_color_mixer(color, *blend, edge_alpha) : 
                                 sgl_color_mixer(sgl_color_mixer(color, *blend, edge_alpha), *blend, alpha));
                    }
                    else {
                        *blend = (alpha == SGL_ALPHA_MAX) ? color : sgl_color_mixer(color, *blend, alpha);
                    }
                }
            }
        }
        buf += surf->w;
    }
}


/**
 * @brief fill a round rectangle with alpha and border
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param radius radius of round
 * @param color color of rectangle
 * @param border_color color of border
 * @param border_width width of border
 * @param alpha alpha of rectangle
 * @param border_alpha alpha of border
 * @return none
 */
void sgl_draw_fill_rect_with_border(sgl_surf_t *surf, sgl_area_t *area, sgl_area_t *rect, int16_t radius, sgl_color_t color, sgl_color_t border_color, uint8_t border_width, uint8_t alpha, uint8_t border_alpha)
{
    sgl_area_t clip = SGL_AREA_INVALID;
    sgl_color_t *buf = NULL, *blend = NULL;

    sgl_surf_clip_area_return(surf, area, &clip);
    if (!sgl_area_selfclip(&clip, rect)) return;

    buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, clip.y1 - surf->y1);
    const int cx1i = rect->x1 + border_width;
    const int cx2i = rect->x2 - border_width;
    const int cyi1 = rect->y1 + border_width;
    const int cyi2 = rect->y2 - border_width;

    if (radius == 0) {
        for (int y = clip.y1; y <= clip.y2; y++) {
            blend = buf;
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                if (x >= cx1i && x <= cx2i && y >= cyi1 && y <= cyi2) {
                    *blend = (alpha == SGL_ALPHA_MAX) ? color : sgl_color_mixer(color, *blend, alpha);
                } else {
                    *blend = (border_alpha == SGL_ALPHA_MAX) ? border_color : sgl_color_mixer(border_color, *blend, border_alpha);
                }
            }
            buf += surf->w;
        }
        return;
    }

    const int cx1 = rect->x1 + radius;
    const int cx2 = rect->x2 - radius;
    const int cy1 = rect->y1 + radius;
    const int cy2 = rect->y2 - radius;

    const int radius_in = sgl_max(radius - border_width, 0);
    const int out_r2 = sgl_pow2(radius);
    const int out_r2_max = sgl_pow2(radius + 1);
    const int in_r2 = sgl_pow2(radius_in);
    const int in_r2_max = sgl_pow2(radius_in + 1);

    const int out_r2_diff = sgl_max(out_r2_max - out_r2, 1);
    const int out_fix_diff = (SGL_ALPHA_MAX << SGL_FIXED_SHIFT) / out_r2_diff;
    const int in_r2_diff = sgl_max(in_r2_max - in_r2, 1);
    const int in_fix_diff = (SGL_ALPHA_MAX << SGL_FIXED_SHIFT) / in_r2_diff;

    int cy_tmp, cx_tmp;
    int dy2, real_r2;
    uint8_t edge_alpha;
    sgl_color_t edge_c;

    for (int y = clip.y1; y <= clip.y2; y++) {
        blend = buf;

        if (y >= cy1 && y <= cy2) {
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                if (x < cx1i || x > cx2i) {
                    *blend = (border_alpha == SGL_ALPHA_MAX) ? border_color : sgl_color_mixer(border_color, *blend, border_alpha);
                } else {
                    *blend = (alpha == SGL_ALPHA_MAX) ? color : sgl_color_mixer(color, *blend, alpha);
                }
            }
        }
        else {
            cy_tmp = (y < cy1) ? cy1 : cy2;
            dy2 = sgl_pow2(y - cy_tmp);

            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                if (x >= cx1 && x <= cx2) {
                    if (y < cyi1 || y > cyi2) {
                        *blend = (border_alpha == SGL_ALPHA_MAX) ? border_color : sgl_color_mixer(border_color, *blend, border_alpha);
                    } else {
                        *blend = (alpha == SGL_ALPHA_MAX) ? color : sgl_color_mixer(color, *blend, alpha);
                    }
                }
                else {
                    cx_tmp = (x < cx1) ? cx1 : cx2;
                    real_r2 = sgl_pow2(x - cx_tmp) + dy2;

                    if (real_r2 >= out_r2_max) {
                        if(x > cx_tmp) break; else continue;
                    }
                    else if (real_r2 < in_r2) {
                        *blend = (alpha == SGL_ALPHA_MAX) ? color : sgl_color_mixer(color, *blend, alpha);
                    }
                    else if (real_r2 < in_r2_max) {
                        edge_alpha = ((in_r2_max - real_r2) * in_fix_diff) >> SGL_FIXED_SHIFT;
                        edge_c = sgl_color_mixer(sgl_color_mixer(color, *blend, alpha), border_color, edge_alpha);
                        *blend = (border_alpha == SGL_ALPHA_MAX) ? edge_c : sgl_color_mixer(edge_c, *blend, border_alpha);
                    }
                    else if (real_r2 <= out_r2) {
                        *blend = (border_alpha == SGL_ALPHA_MAX) ? border_color : sgl_color_mixer(border_color, *blend, border_alpha);
                    }
                    else {
                        edge_alpha = ((out_r2_max - real_r2) * out_fix_diff) >> SGL_FIXED_SHIFT;
                        edge_c = sgl_color_mixer(border_color, *blend, edge_alpha);
                        *blend = (border_alpha == SGL_ALPHA_MAX) ? edge_c : sgl_color_mixer(edge_c, *blend, border_alpha);
                    }
                }
            }
        }
        buf += surf->w;
    }
}


#if (!CONFIG_SGL_PIXMAP_BILINEAR_INTERP)
/**
 * @brief fill a round rectangle pixmap with alpha
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param radius radius of round
 * @param pixmap pixmap of rectangle
 * @param alpha alpha of rectangle
 * @return none
 */
void sgl_draw_fill_rect_pixmap(sgl_surf_t *surf, sgl_area_t *area, sgl_area_t *rect, int16_t radius, const sgl_pixmap_t *pixmap, uint8_t alpha)
{
    sgl_area_t clip = SGL_AREA_INVALID;
    sgl_color_t *buf = NULL, *blend = NULL, *pbuf = (sgl_color_t *)pixmap->bitmap.array;
    
    sgl_surf_clip_area_return(surf, area, &clip);
    if (!sgl_area_selfclip(&clip, rect)) return;

    const int32_t rect_w = rect->x2 - rect->x1 + 1;
    const int32_t rect_h = rect->y2 - rect->y1 + 1;
    const int32_t scale_x = ((int32_t)pixmap->width << SGL_FIXED_SHIFT) / rect_w;
    const int32_t scale_y = ((int32_t)pixmap->height << SGL_FIXED_SHIFT) / rect_h;
    uint32_t step_x = 0, step_y = 0;

    buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, clip.y1 - surf->y1);
    if (radius == 0) {
        for (int y = clip.y1; y <= clip.y2; y++) {
            blend = buf;
            step_y = (scale_y * (y - rect->y1)) >> SGL_FIXED_SHIFT;
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                step_x = (scale_x * (x - rect->x1)) >> SGL_FIXED_SHIFT;
                pbuf = sgl_pixmap_get_buf(pixmap, step_x, step_y);
                *blend = (alpha == SGL_ALPHA_MAX ? *pbuf : sgl_color_mixer(*pbuf, *blend, alpha));
            }
            buf += surf->w;
        }
        return;
    }

    const int cx1 = rect->x1 + radius;
    const int cx2 = rect->x2 - radius;
    const int cy1 = rect->y1 + radius;
    const int cy2 = rect->y2 - radius;

    const int r2 = sgl_pow2(radius);
    const int r2_max = sgl_pow2(radius + 1);
    const int r2_diff = sgl_max(r2_max - r2, 1);
    const int r2_fix_diff = (SGL_ALPHA_MAX << SGL_FIXED_SHIFT) / r2_diff;
    int dy2, real_r2, cx_tmp, cy_tmp;
    uint8_t edge_alpha;

    for (int y = clip.y1; y <= clip.y2; y++) {
        blend = buf;
        step_y = (scale_y * (y - rect->y1)) >> SGL_FIXED_SHIFT;
        if (y >= cy1 && y <= cy2) {
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                step_x = (scale_x * (x - rect->x1)) >> SGL_FIXED_SHIFT;
                pbuf = sgl_pixmap_get_buf(pixmap, step_x, step_y);
                *blend = (alpha == SGL_ALPHA_MAX ? *pbuf : sgl_color_mixer(*pbuf, *blend, alpha));
            }
        }
        else {
            cy_tmp = (y < cy1) ? cy1 : cy2;
            dy2 = sgl_pow2(y - cy_tmp);

            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                step_x = (scale_x * (x - rect->x1)) >> SGL_FIXED_SHIFT;
                pbuf = sgl_pixmap_get_buf(pixmap, step_x, step_y);

                if (x >= cx1 && x <= cx2) {
                    *blend = (alpha == SGL_ALPHA_MAX ? *pbuf : sgl_color_mixer(*pbuf, *blend, alpha));
                }
                else {
                    cx_tmp = (x < cx1) ? cx1 : cx2;
                    real_r2 = sgl_pow2(x - cx_tmp) + dy2;
                    if (real_r2 >= r2_max) {
                        if(x > cx_tmp) break; else continue;
                    }
                    else if (real_r2 >= r2) {
                        edge_alpha = ((r2_max - real_r2) * r2_fix_diff) >> SGL_FIXED_SHIFT;
                        *blend = (alpha == SGL_ALPHA_MAX ? sgl_color_mixer(*pbuf, *blend, edge_alpha) : sgl_color_mixer(sgl_color_mixer(*pbuf, *blend, edge_alpha), *blend, alpha));
                    }
                    else {
                        *blend = (alpha == SGL_ALPHA_MAX ? *pbuf : sgl_color_mixer(*pbuf, *blend, alpha));
                    }
                }
            }
        }
        buf += surf->w;
    }
}

#else
/**
 * @brief fill a round rectangle pixmap with alpha
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param radius radius of round
 * @param pixmap pixmap of rectangle
 * @param alpha alpha of rectangle
 * @return none
 */
void sgl_draw_fill_rect_pixmap(sgl_surf_t *surf, sgl_area_t *area, sgl_area_t *rect, int16_t radius, const sgl_pixmap_t *pixmap, uint8_t alpha)
{
    sgl_area_t clip = SGL_AREA_INVALID;
    sgl_color_t *buf = NULL, *blend = NULL, *pbuf = (sgl_color_t *)pixmap->bitmap.array, ip_color;
    
    sgl_surf_clip_area_return(surf, area, &clip);
    if (!sgl_area_selfclip(&clip, rect)) return;

    int cx_tmp = 0, fx = 0;
    int cy_tmp = 0, fy = 0;
    const int32_t rect_w = rect->x2 - rect->x1 + 1;
    const int32_t rect_h = rect->y2 - rect->y1 + 1;
    const int32_t scale_x = ((int32_t)pixmap->width << SGL_FIXED_SHIFT) / rect_w;
    const int32_t scale_y = ((int32_t)pixmap->height << SGL_FIXED_SHIFT) / rect_h;
    buf = sgl_surf_get_buf(surf, clip.x1 - surf->x1, clip.y1 - surf->y1);

    if (radius == 0) {
        for (int y = clip.y1; y <= clip.y2; y++) {
            blend = buf;
            fy = (int32_t)(y - rect->y1) * scale_y;
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                fx = (int32_t)(x - rect->x1) * scale_x;
                ip_color = sgl_draw_biln_color(pbuf, pixmap->width, pixmap->height, fx, fy);
                *blend = (alpha == SGL_ALPHA_MAX) ? ip_color : sgl_color_mixer(ip_color, *blend, alpha);
            }
            buf += surf->w;
        }
        return;
    }

    const int cx1 = rect->x1 + radius;
    const int cx2 = rect->x2 - radius;
    const int cy1 = rect->y1 + radius;
    const int cy2 = rect->y2 - radius;

    const int r2 = sgl_pow2(radius);
    const int r2_max = sgl_pow2(radius + 1);
    const int r2_diff = sgl_max(r2_max - r2, 1);
    const int r2_fix_diff = (SGL_ALPHA_MAX << SGL_FIXED_SHIFT) / r2_diff;
    int dy2, real_r2;
    uint8_t edge_alpha;

    for (int y = clip.y1; y <= clip.y2; y++) {
        blend = buf;
        fy = (int32_t)(y - rect->y1) * scale_y;
        if (y >= cy1 && y <= cy2) {
            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                fx = (int32_t)(x - rect->x1) * scale_x;
                ip_color = sgl_draw_biln_color(pbuf, pixmap->width, pixmap->height, fx, fy);
                *blend = (alpha == SGL_ALPHA_MAX) ? ip_color : sgl_color_mixer(ip_color, *blend, alpha);
            }
        }
        else {
            cy_tmp = (y < cy1) ? cy1 : cy2;
            dy2 = sgl_pow2(y - cy_tmp);

            for (int x = clip.x1; x <= clip.x2; x++, blend++) {
                fx = (int32_t)(x - rect->x1) * scale_x;
                ip_color = sgl_draw_biln_color(pbuf, pixmap->width, pixmap->height, fx, fy);

                if (x >= cx1 && x <= cx2) {
                    *blend = (alpha == SGL_ALPHA_MAX) ? ip_color : sgl_color_mixer(ip_color, *blend, alpha);
                }
                else {
                    cx_tmp = (x < cx1) ? cx1 : cx2;
                    real_r2 = sgl_pow2(x - cx_tmp) + dy2;
                    if (real_r2 >= r2_max) {
                        if(x > cx_tmp) break; else continue;
                    }
                    else if (real_r2 >= r2) {
                        edge_alpha = ((r2_max - real_r2) * r2_fix_diff) >> SGL_FIXED_SHIFT;
                        *blend = (alpha == SGL_ALPHA_MAX ? sgl_color_mixer(ip_color, *blend, edge_alpha) : 
                                 sgl_color_mixer(sgl_color_mixer(ip_color, *blend, edge_alpha), *blend, alpha));
                    }
                    else {
                        *blend = (alpha == SGL_ALPHA_MAX) ? ip_color : sgl_color_mixer(ip_color, *blend, alpha);
                    }
                }
            }
        }
        buf += surf->w;
    }
}
#endif


/**
 * @brief fill a round rectangle with alpha
 * @param surf point to surface
 * @param area area of rectangle that you want to draw
 * @param rect point to rectangle that you want to draw
 * @param desc rectangle description
 * @return none
 */
void sgl_draw_rect(sgl_surf_t *surf, sgl_area_t *area, sgl_rect_t *rect, sgl_draw_rect_t *desc)
{
    if (desc->pixmap == NULL) {
        if (desc->border == 0) {
            sgl_draw_fill_rect(surf, area, rect, desc->radius, desc->color, desc->alpha);
        }
        else {
            sgl_draw_fill_rect_with_border(surf, area, rect, desc->radius, desc->color, desc->border_color, desc->border, desc->alpha, desc->border_alpha);
        }
    }
    else {
        sgl_draw_fill_rect_pixmap(surf, area, rect, desc->radius, desc->pixmap, desc->alpha);
    }
}
