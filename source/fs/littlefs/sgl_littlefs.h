/* source/fs/littlefs/sgl_littlefs.h
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
#ifndef __SGL_LITTLEFS_H__
#define __SGL_LITTLEFS_H__

#include <sgl_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LittleFS-like mount configuration
 * @block_size:  block/erase size in bytes (must be multiple of 512)
 * @block_count: total number of blocks on the device
 *
 * Note: read_size, prog_size, cache_size, lookahead_size, block_cycles
 * are kept for API compatibility but not used by this implementation.
 */
typedef struct {
    uint32_t read_size;
    uint32_t prog_size;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t cache_size;
    uint32_t lookahead_size;
    int32_t  block_cycles;
} sgl_littlefs_config_t;

/**
 * @brief Register LittleFS-like file system type with sgl_fs VFS.
 *        Call this once before sgl_fs_mount("littlefs", ...).
 * @return 0 on success, -1 on failure
 */
int sgl_littlefs_register(void);

#ifdef __cplusplus
}
#endif

#endif
