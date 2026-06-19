/* sgl_littlefs.h
 *
 * MIT License
 * Copyright(c) 2023-present All contributors of SGL
 *
 * Self-contained LittleFS-like filesystem for sgl_fs VFS layer.
 * No external dependencies.
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
