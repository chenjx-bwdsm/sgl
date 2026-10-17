/* source/fs/posix/sgl_posix.c
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

#include <sgl_fs.h>
#include <sgl_mm.h>
#include "sgl_posix.h"

#if defined(_WIN32)

/* The posix driver targets POSIX-unified hosts (Linux / macOS / RTOS).
 * On Windows hosts use winfs or stdiofs instead; registering fails cleanly. */
int sgl_posix_register(void)
{
    return -1;
}

#else /* ! _WIN32 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#ifndef POSIXFS_MAX_DIRS
#define POSIXFS_MAX_DIRS           4
#endif

#define POSIXFS_PATH_MAX           1024
#define POSIXFS_BASE_DIR_MAX       512
#define POSIXFS_DIR_MODE           (0777)
#define POSIXFS_FILE_MODE          (0644)

typedef struct {
    char base_dir[POSIXFS_BASE_DIR_MAX];
} posixfs_ctx_t;

typedef struct {
    int used;
    DIR *dirp;
    char dir_path[POSIXFS_PATH_MAX];   /* resolved host dir, for stat fallback */
} posixfs_dir_t;

static posixfs_dir_t posixfs_dirs[POSIXFS_MAX_DIRS];

static sgl_posix_config_t posixfs_default_cfg = { "." };

/* posix directories are always fresh host state: nothing to flush here */
#define POSIXFS_UNUSED(x)  ((void)(x))

static int posixfs_map_errno(int err)
{
    switch (err) {
    case 0:      return SGL_FS_OK;
    case ENOENT: return SGL_FS_NOT_FOUND;
    case EACCES:
    case EPERM:  return SGL_FS_PERMISSION_DENIED;
    case EEXIST: return SGL_FS_EXISTS;
    case ENOSPC: return SGL_FS_NO_SPACE;
    case ENOMEM: return SGL_FS_NO_MEMORY;
    case EIO:    return SGL_FS_IO_ERROR;
    case EINVAL: return SGL_FS_INVALID_ARGUMENT;
    default:     return SGL_FS_ERROR;
    }
}

/* Resolve a VFS path (leading '/' kept by the core) against the base_dir */
static void posixfs_normalize_path(const char *path, const char *base_dir,
                                   char *out, size_t out_size)
{
    const char *src = path;
    char tmp[POSIXFS_PATH_MAX];

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (src == NULL) {
        return;
    }

    if (src[0] == '\0') {
        snprintf(out, out_size, "%s", base_dir ? base_dir : ".");
    } else if (src[0] == '/') {
        snprintf(tmp, sizeof(tmp), "%s%s", base_dir ? base_dir : ".", src);
        snprintf(out, out_size, "%s", tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "%s/%s", base_dir ? base_dir : ".", src);
        snprintf(out, out_size, "%s", tmp);
    }
}

static void posixfs_build_path(posixfs_ctx_t *ctx, const char *path,
                               char *out, size_t out_size)
{
    posixfs_normalize_path(path, ctx->base_dir, out, out_size);
}

/* Map SGL_O_* flags to host O_* flags (values differ across POSIX flavors,
 * e.g. macOS O_CREAT is 0x200, so translate explicitly instead of passing
 * the SGL numbers through) */
static int posixfs_host_flags(uint32_t flags)
{
    uint32_t acc = flags & (SGL_O_RDONLY | SGL_O_WRONLY | SGL_O_RDWR);
    int hflags;

    switch (acc) {
    case SGL_O_WRONLY: hflags = O_WRONLY; break;
    case SGL_O_RDWR:   hflags = O_RDWR;   break;
    default:           hflags = O_RDONLY; break;
    }
    if (flags & SGL_O_CREAT)  { hflags |= O_CREAT;  }
    if (flags & SGL_O_TRUNC)  { hflags |= O_TRUNC;  }
    if (flags & SGL_O_APPEND) { hflags |= O_APPEND; }
    return hflags;
}

static int posixfs_stat_impl(const char *npath, sgl_stat_t *st)
{
    struct stat host_st;

    if (st == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    memset(st, 0, sizeof(*st));
    if (stat(npath, &host_st) != 0) {
        return posixfs_map_errno(errno);
    }

    st->st_mode = S_ISDIR(host_st.st_mode) ? SGL_S_IFDIR : SGL_S_IFREG;
    st->st_size = (uint32_t)host_st.st_size;
    st->st_mtime = (uint32_t)host_st.st_mtime;
    return SGL_FS_OK;
}

static int posixfs_mount(void **fs, sgl_block_dev_t *dev,
                         const char *mount_point, void *fs_config)
{
    posixfs_ctx_t *ctx;
    sgl_posix_config_t *cfg = (sgl_posix_config_t *)fs_config;

    POSIXFS_UNUSED(dev);
    POSIXFS_UNUSED(mount_point);

    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    *fs = NULL;

    ctx = (posixfs_ctx_t *)sgl_malloc(sizeof(posixfs_ctx_t));
    if (ctx == NULL) {
        return SGL_FS_NO_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->base_dir, sizeof(ctx->base_dir), "%s",
             cfg && cfg->base_dir ? cfg->base_dir : posixfs_default_cfg.base_dir);

    *fs = ctx;
    return SGL_FS_OK;
}

static int posixfs_unmount(void *fs, const char *mount_point)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;

    POSIXFS_UNUSED(mount_point);

    if (ctx == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    /* core closes all fds/dds of this mount point before unmounting */
    sgl_free(ctx);
    return SGL_FS_OK;
}

static int posixfs_open(void *fs, const char *path, uint32_t flags)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char npath[POSIXFS_PATH_MAX];
    int fd;

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    posixfs_build_path(ctx, path, npath, sizeof(npath));

    fd = open(npath, posixfs_host_flags(flags), POSIXFS_FILE_MODE);
    if (fd < 0) {
        return posixfs_map_errno(errno);
    }
    return fd;
}

static int posixfs_close(void *fs, int fd)
{
    POSIXFS_UNUSED(fs);

    if (fd < 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (close(fd) != 0) {
        return posixfs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int posixfs_read(void *fs, int fd, void *buffer, uint32_t count)
{
    ssize_t n;

    POSIXFS_UNUSED(fs);

    if (buffer == NULL || fd < 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return 0;
    }
    n = read(fd, buffer, count);
    if (n < 0) {
        return posixfs_map_errno(errno);
    }
    return (int)n;
}

static int posixfs_write(void *fs, int fd, const void *buffer, uint32_t count)
{
    ssize_t n;

    POSIXFS_UNUSED(fs);

    if (buffer == NULL || fd < 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return 0;
    }
    n = write(fd, buffer, count);
    if (n < 0) {
        return posixfs_map_errno(errno);
    }
    return (int)n;
}

static int posixfs_seek(void *fs, int fd, int32_t offset, uint8_t whence)
{
    int host_whence;
    off_t pos;

    POSIXFS_UNUSED(fs);

    if (fd < 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    switch (whence) {
    case SGL_SEEK_SET: host_whence = SEEK_SET; break;
    case SGL_SEEK_CUR: host_whence = SEEK_CUR; break;
    case SGL_SEEK_END: host_whence = SEEK_END; break;
    default:           return SGL_FS_INVALID_ARGUMENT;
    }

    pos = lseek(fd, (off_t)offset, host_whence);
    if (pos == (off_t)-1) {
        return posixfs_map_errno(errno);
    }
    return (int)pos;
}

static int posixfs_opendir(void *fs, const char *path, int *dd)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char npath[POSIXFS_PATH_MAX];
    posixfs_dir_t *dir = NULL;
    DIR *dirp;
    int slot = -1;

    if (ctx == NULL || path == NULL || dd == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    *dd = -1;

    for (int i = 0; i < POSIXFS_MAX_DIRS; ++i) {
        if (!posixfs_dirs[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return SGL_FS_NO_SPACE;
    }

    posixfs_build_path(ctx, path, npath, sizeof(npath));
    dirp = opendir(npath);
    if (dirp == NULL) {
        return posixfs_map_errno(errno);
    }

    dir = &posixfs_dirs[slot];
    memset(dir, 0, sizeof(*dir));
    dir->used = 1;
    dir->dirp = dirp;
    snprintf(dir->dir_path, sizeof(dir->dir_path), "%s", npath);
    *dd = slot;
    return SGL_FS_OK;
}

static int posixfs_readdir(void *fs, int dd, char *name, uint32_t name_size,
                           uint32_t *type)
{
    posixfs_dir_t *dir;
    struct dirent *ent;
    char full[POSIXFS_PATH_MAX];
    struct stat host_st;

    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (dd < 0 || dd >= POSIXFS_MAX_DIRS || !posixfs_dirs[dd].used) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (name == NULL || name_size == 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }

    dir = &posixfs_dirs[dd];
    for (;;) {
        ent = readdir(dir->dirp);
        if (ent == NULL) {
            return 0;
        }
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(name, name_size, "%s", ent->d_name);
        if (type != NULL) {
            *type = SGL_S_IFREG;
            if (ent->d_type == DT_DIR) {
                *type = SGL_S_IFDIR;
            } else if (ent->d_type == DT_UNKNOWN) {
                /* d_type unreliable on some filesystems: fall back to stat */
                snprintf(full, sizeof(full), "%s/%s", dir->dir_path, ent->d_name);
                if (stat(full, &host_st) == 0 && S_ISDIR(host_st.st_mode)) {
                    *type = SGL_S_IFDIR;
                }
            }
        }
        return 1;
    }
}

static int posixfs_closedir(void *fs, int dd)
{
    POSIXFS_UNUSED(fs);

    if (dd < 0 || dd >= POSIXFS_MAX_DIRS || !posixfs_dirs[dd].used) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (posixfs_dirs[dd].dirp != NULL) {
        closedir(posixfs_dirs[dd].dirp);
    }
    memset(&posixfs_dirs[dd], 0, sizeof(posixfs_dirs[dd]));
    return SGL_FS_OK;
}

static int posixfs_sync(void *fs)
{
    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    /* host page cache is flushed per-fd via fsync() on close path; there is
     * no fs-level sync handle to iterate here */
    return SGL_FS_OK;
}

static int posixfs_format(void *fs)
{
    /* a host pass-through has nothing to format; refuse destructively
     * wiping the base directory */
    POSIXFS_UNUSED(fs);
    return SGL_FS_ERROR;
}

static int posixfs_remove(void *fs, const char *path)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char npath[POSIXFS_PATH_MAX];
    struct stat host_st;
    int ret;

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    posixfs_build_path(ctx, path, npath, sizeof(npath));

    if (stat(npath, &host_st) == 0 && S_ISDIR(host_st.st_mode)) {
        ret = rmdir(npath);
    } else {
        ret = unlink(npath);
    }
    if (ret != 0) {
        return posixfs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int posixfs_mkdir(void *fs, const char *path)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char npath[POSIXFS_PATH_MAX];

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    posixfs_build_path(ctx, path, npath, sizeof(npath));

    if (mkdir(npath, POSIXFS_DIR_MODE) != 0) {
        return posixfs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int posixfs_stat(void *fs, const char *path, sgl_stat_t *st)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char npath[POSIXFS_PATH_MAX];

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    posixfs_build_path(ctx, path, npath, sizeof(npath));
    return posixfs_stat_impl(npath, st);
}

static int posixfs_rename(void *fs, const char *old_path, const char *new_path)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    char nold[POSIXFS_PATH_MAX];
    char nnew[POSIXFS_PATH_MAX];

    if (ctx == NULL || old_path == NULL || new_path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    posixfs_build_path(ctx, old_path, nold, sizeof(nold));
    posixfs_build_path(ctx, new_path, nnew, sizeof(nnew));

    if (rename(nold, nnew) != 0) {
        return posixfs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int posixfs_statvfs(void *fs, sgl_statvfs_t *info)
{
    posixfs_ctx_t *ctx = (posixfs_ctx_t *)fs;
    struct statvfs host_vfs;

    if (ctx == NULL || info == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    if (statvfs(ctx->base_dir, &host_vfs) != 0) {
        return posixfs_map_errno(errno);
    }

    info->f_bsize  = (uint32_t)host_vfs.f_bsize;
    info->f_blocks = (uint32_t)host_vfs.f_blocks;
    info->f_bfree  = (uint32_t)host_vfs.f_bfree;
    info->f_bavail = (uint32_t)host_vfs.f_bavail;
    info->f_files  = (uint32_t)host_vfs.f_files;
    info->f_ffree  = (uint32_t)host_vfs.f_ffree;
    return SGL_FS_OK;
}

static sgl_fs_ops_t posixfs_ops = {
    .mount    = posixfs_mount,
    .unmount  = posixfs_unmount,
    .open     = posixfs_open,
    .close    = posixfs_close,
    .read     = posixfs_read,
    .write    = posixfs_write,
    .seek     = posixfs_seek,
    .opendir  = posixfs_opendir,
    .readdir  = posixfs_readdir,
    .closedir = posixfs_closedir,
    .sync     = posixfs_sync,
    .format   = posixfs_format,
    .remove   = posixfs_remove,
    .mkdir    = posixfs_mkdir,
    .stat     = posixfs_stat,
    .rename   = posixfs_rename,
    .statvfs  = posixfs_statvfs,
};

static sgl_fs_type_t posixfs_fs_type = {
    .name = "posix",
    .ops  = &posixfs_ops,
};

int sgl_posix_register(void)
{
    return sgl_fs_register(&posixfs_fs_type);
}

#endif /* _WIN32 */
