/* source/fs/posix/sgl_posix.h
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

#ifndef __SGL_POSIX_H__
#define __SGL_POSIX_H__

#include <sgl_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief posixfs mount configuration
 * @base_dir: host directory all mounted paths are resolved against.
 *            NULL falls back to the current working directory (".").
 */
typedef struct {
    const char *base_dir;
} sgl_posix_config_t;

/**
 * @brief Register the posix (POSIX-unified API) filesystem type with sgl_fs VFS.
 *        Call this once before sgl_fs_mount("posix", ...).
 *        On Windows hosts the driver is unavailable and registration fails;
 *        use winfs / stdiofs there instead.
 * @return 0 on success, -1 on failure (e.g. already registered, or not POSIX)
 */
int sgl_posix_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_POSIX_H__ */
