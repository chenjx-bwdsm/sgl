/* source/include/sgl_misc.h
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

#ifndef __SGL_MISC_H__
#define __SGL_MISC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <sgl_cfgfix.h>
#include <stddef.h>
#include <sgl_list.h>
#include <sgl_types.h>

#if (CONFIG_SGL_BOOT_LOGO)

/**
 * @brief to show the sgl logo after sgl init
 * @param none
 * @return none
 * @note: you can call this function in your main function to show the sgl logo
 */
void sgl_boot_logo(void);

#endif // ! CONFIG_SGL_BOOT_LOGO

#if (CONFIG_SGL_MONITOR_TRACE)
#define  SGL_MONITOR_COORDS_WIDTH       (81)
#define  SGL_MONITOR_COORDS_HEIGHT      (30)
#define  SGL_MONITOR_COORDS_X           (SGL_SCREEN_WIDTH - SGL_MONITOR_COORDS_WIDTH)
#define  SGL_MONITOR_COORDS_Y           (SGL_SCREEN_HEIGHT - SGL_MONITOR_COORDS_HEIGHT)
#define  SGL_MONITOR_COLOR              (SGL_COLOR_BLACK)
#define  SGL_MONITOR_TEXT_COLOR         (SGL_COLOR_WHITE)
#define  SGL_MONITOR_ALPHA              (128)

#define  SGL_MONITOR_COORDS             (sgl_area_t){.x1 = SGL_MONITOR_COORDS_X,     \
                                                     .x2 = SGL_MONITOR_COORDS_X + SGL_SCREEN_WIDTH - 1,     \
                                                     .y1 = SGL_MONITOR_COORDS_Y,     \
                                                     .y2 = SGL_MONITOR_COORDS_Y + SGL_SCREEN_HEIGHT - 1,    \
                                                    }

void sgl_monitor_trace(sgl_surf_t *surf);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // ! __SGL_MISC_H__
