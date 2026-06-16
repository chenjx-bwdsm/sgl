/* source/include/sgl_fs.h
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

#ifndef __SGL_FS_H__
#define __SGL_FS_H__

#include "sgl_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SGL_S_IFMT   0170000  /* mask of file type */
#define SGL_S_IFREG  0100000  /* regular file */
#define SGL_S_IFDIR  0040000  /* directory file */

#define SGL_S_IRWXU  00700    /* owner permission */
#define SGL_S_IRWXG  00070    /* group permission */
#define SGL_S_IRWXO  00007    /* other permission */

#define SGL_MAX_FD   (8)      /* max file descriptor */
#define SGL_MAX_DD   (4)      /* max directory descriptor */

/**
 * @brief File status
 * @st_mode: file type and permission
 * @st_size: file size, in bytes
 * @st_mtime: last modify time
 */
typedef struct {
    uint32_t st_mode;
    uint32_t st_size;
    uint32_t st_mtime;
} sgl_stat_t;

/**
 * @brief File system device
 * @init: init disk device
 * @read_sectors: read sectors from disk devices
 * @write_sector: write sector to disk devices
 * @ioctl: ioctl command, it is optional
 * @status: status command, it is optional
 * @priv: private data
 */
typedef struct sgl_block_dev {
    int (*init)(struct sgl_block_dev *dev);
    int (*read_sectors)(struct sgl_block_dev *dev, uint32_t sector, uint8_t *buf, uint32_t count);
    int (*write_sectors)(struct sgl_block_dev *dev, uint32_t sector, const uint8_t *buf, uint32_t count);
    int (*ioctl)(struct sgl_block_dev *dev, uint8_t cmd, void *param);
    int (*status)(struct sgl_block_dev *dev);
    void *priv;
} sgl_block_dev_t;

/**
 * @brief File system operations
 * @mount: mount file system
 * @unmount: unmount file system
 * @open: open file
 * @close: close file
 * @read: read file
 * @write: write file
 * @opendir: open directory
 * @readdir: read directory
 * @closedir: close directory
 * @sync: sync file system
 * @format: format file system
 * @remove: remove file
 * @mkdir: make directory
 * @stat: get file status
 * @rename: rename file
 */
typedef struct sgl_fs_ops {
    int (*mount)(void **fs, struct sgl_block_dev *dev, const char *mount_point, void *fs_config);
    int (*unmount)(void *fs, const char *mount_point);

    int (*open)(void *fs, const char *path, uint32_t flags);
    int (*close)(void *fs, int fd);
    int (*read)(void *fs, int fd, void *buffer, uint32_t count);
    int (*write)(void *fs, int fd, const void *buffer, uint32_t count);

    int (*opendir)(void *fs, const char *path, int *dd);
    int (*readdir)(void *fs, int dd, char *name, uint32_t name_size, uint32_t *type);
    int (*closedir)(void *fs, int dd);

    int (*sync)(void *fs);
    int (*format)(void *fs);
    int (*remove)(void *fs, const char *path);
    int (*mkdir)(void *fs, const char *path);

    int (*stat)(void *fs, const char *path, sgl_stat_t *st);
    int (*rename)(void *fs, const char *old_path, const char *new_path);
} sgl_fs_ops_t;

/**
 * @brief File system type
 * @name: file system name
 * @ops: file system operations
 */
typedef struct sgl_fs_type {
    sgl_list_node_t node;
    const char *name;
    sgl_fs_ops_t *ops;
} sgl_fs_type_t;

/**
 * @brief Mount point
 * @path: mount point path
 * @dev: block device
 * @fs_type: file system type
 * @fs_data: file system data
 */
typedef struct sgl_mount_point {
    sgl_list_node_t node;
    const char *path;
    sgl_block_dev_t *dev;
    sgl_fs_type_t *fs_type;
    void *fs_data;
} sgl_mount_point_t;

/**
 * @brief File descriptor control block
 * @used: used flag
 * @local_fd: local file descriptor
 * @mp: mount point
 */
typedef struct {
    uint16_t used;
    int16_t  local_fd;
    sgl_mount_point_t *mp;
} sgl_fd_ctrl_t;

/**
 * @brief Directory descriptor control block
 * @used: used flag
 * @local_dd: local directory descriptor
 * @mp: mount point
 */
typedef struct {
    uint16_t used;
    int16_t  local_dd;
    sgl_mount_point_t *mp;
} sgl_dd_ctrl_t;

/**
 * @brief Register a file system type
 * @param fs_type File system type pointer
 * @return 0 on success, -1 on failure
 */
int sgl_fs_register(sgl_fs_type_t *fs_type);

/**
 * @brief Mount a file system
 * @param mount_point Mount point path
 * @param fs_name File system name
 * @param dev Block device pointer
 * @param fs_config File system configuration pointer
 * @return 0 on success, -1 on failure
 */
int sgl_fs_mount(const char *mount_point, const char *fs_name, sgl_block_dev_t *dev, void *fs_config);

/**
 * @brief Open a file
 * @param path Path to open
 * @param flags Open flags
 * @return File descriptor, or -1 on failure
 */
int sgl_open(const char *path, uint32_t flags);

/**
 * @brief Close a file
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int sgl_close(int fd);

/**
 * @brief Read from a file
 * @param fd File descriptor
 * @param buffer Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on failure
 */
int sgl_read(int fd, void *buffer, uint32_t count);

/**
 * @brief Write to a file
 * @param fd File descriptor
 * @param buffer Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on failure
 */
int sgl_write(int fd, const void *buffer, uint32_t count);

/**
 * @brief Get file status
 * @param path Path to get status for
 * @param st Pointer to store status
 * @return 0 on success, -1 on failure
 */
int sgl_stat(const char *path, sgl_stat_t *st);

/**
 * @brief Open a directory
 * @param path Path to open
 * @param dd Pointer to store directory descriptor
 * @return 0 on success, -1 on failure
 */
int sgl_opendir(const char *path, int *dd);

/**
 * @brief Read a directory entry
 * @param dd Directory descriptor
 * @param name Buffer to store name
 * @param name_size Size of name buffer
 * @param type Pointer to store type
 * @return 0 on success, -1 on failure
 */
int sgl_readdir(int dd, char *name, uint32_t name_size, uint32_t *type);

/**
 * @brief Close a directory
 * @param dd Directory descriptor
 * @return 0 on success, -1 on failure
 */
int sgl_closedir(int dd);

/**
 * @brief Unmount a filesystem
 * @param mount_point Mount point to unmount
 * @return 0 on success, -1 on failure
 */
int sgl_unmount(const char *mount_point);

/**
 * @brief Synchronize a filesystem
 * @param path Path to synchronize, or NULL to synchronize all mounted filesystems
 * @return 0 on success, -1 on failure
 */
int sgl_sync(const char *path);

/**
 * @brief Format a filesystem
 * @param mount_point Mount point to format
 * @param fs_config Filesystem configuration
 * @return 0 on success, -1 on failure
 */
int sgl_format(const char *mount_point, void *fs_config);

/**
 * @brief Remove a file or directory
 * @param path Path to remove
 * @return 0 on success, -1 on failure
 */
int sgl_remove(const char *path);

/**
 * @brief Create a directory
 * @param path Path to create
 * @return 0 on success, -1 on failure
 */
int sgl_mkdir(const char *path);

/**
 * @brief Rename a file or directory
 * @param old_path Old path
 * @param new_path New path
 * @return 0 on success, -1 on failure
 */
int sgl_rename(const char *old_path, const char *new_path);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_FS_H__ */
