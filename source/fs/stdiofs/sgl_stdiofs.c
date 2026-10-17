/* source/fs/stdiofs/sgl_stdiofs.c
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
#include "sgl_stdiofs.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define STDIOFS_RMDIR(path)        _rmdir(path)
#define STDIOFS_MKDIR(path)        _mkdir(path)
#define STDIOFS_STAT(path, st)     _stat(path, st)
typedef struct _stat stdiofs_stat_t;
#else
#include <unistd.h>
#include <dirent.h>
#define STDIOFS_RMDIR(path)        rmdir(path)
#define STDIOFS_MKDIR(path)        mkdir(path, 0777)
#define STDIOFS_STAT(path, st)     stat(path, st)
typedef struct stat stdiofs_stat_t;
#endif

/* glibc and macOS define st_mtime as an object macro (expanding to
 * st_mtim.tv_sec / st_mtimespec.tv_sec), which would textually rewrite
 * sgl_stat_t member accesses below. Wrap the host-side access in a helper
 * while the platform macro is still in effect, then remove the macro so the
 * sgl_stat_t member keeps its literal name. */
#ifdef st_mtime
static time_t stdiofs_host_stat_mtime(const stdiofs_stat_t *sb)
{
    return (time_t)sb->st_mtime;
}
#define POSIXFS_STAT_MTIME(st_)  stdiofs_host_stat_mtime(st_)
#undef st_mtime
#else
#define POSIXFS_STAT_MTIME(st_)  ((st_)->st_mtime)
#endif

#ifndef STDIOFS_MAX_FILES
#define STDIOFS_MAX_FILES          8
#endif

#ifndef STDIOFS_MAX_DIRS
#define STDIOFS_MAX_DIRS           4
#endif

#define STDIOFS_PATH_MAX           1024
#define STDIOFS_BASE_DIR_MAX       512

#define STDIOFS_FIND_INVALID       (-1)   /* _findfirst failure, never started */
#define STDIOFS_FIND_EXHAUSTED     (-2)   /* directory fully enumerated       */

typedef struct {
    char base_dir[STDIOFS_BASE_DIR_MAX];
} stdiofs_ctx_t;

typedef struct {
    FILE *fp;
} stdiofs_file_t;

typedef struct {
    int used;
#ifdef _WIN32
    intptr_t handle;               /* STDIOFS_FIND_* or _findfirst handle */
    struct _finddata_t data;       /* current (pending or last) entry     */
    int has_pending;               /* first entry not yet returned        */
#else
    DIR *dirp;
#endif
} stdiofs_dir_t;

static stdiofs_file_t stdiofs_files[STDIOFS_MAX_FILES];
static stdiofs_dir_t  stdiofs_dirs[STDIOFS_MAX_DIRS];

static sgl_stdiofs_config_t stdiofs_default_cfg = { "." };

/* stdiofs directories are always fresh host state: nothing to flush here */
#define STDIOFS_UNUSED(x)  ((void)(x))

static int stdiofs_map_errno(int err)
{
    switch (err) {
    case 0:      return SGL_FS_OK;
    case ENOENT: return SGL_FS_NOT_FOUND;
    case EACCES: return SGL_FS_PERMISSION_DENIED;
    case EEXIST: return SGL_FS_EXISTS;
    case ENOSPC: return SGL_FS_NO_SPACE;
    case ENOMEM: return SGL_FS_NO_MEMORY;
    case EINVAL: return SGL_FS_INVALID_ARGUMENT;
    default:     return SGL_FS_ERROR;
    }
}

/* Resolve a VFS path (leading '/' kept by the core) against the base_dir,
 * mirroring winfs: absolute host paths with a drive letter pass through,
 * separators are normalized to '\\' on Windows. */
static void stdiofs_normalize_path(const char *path, const char *base_dir,
                                   char *out, size_t out_size)
{
    const char *src = path;
    char tmp[STDIOFS_PATH_MAX];

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (src == NULL) {
        return;
    }

    if (src[0] == '\0') {
        snprintf(out, out_size, "%s", base_dir ? base_dir : ".");
        return;
    }

    if (src[1] == ':') {
        snprintf(out, out_size, "%s", src);
    } else if (src[0] == '/' || src[0] == '\\') {
        snprintf(tmp, sizeof(tmp), "%s%s", base_dir ? base_dir : ".", src);
        snprintf(out, out_size, "%s", tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "%s/%s", base_dir ? base_dir : ".", src);
        snprintf(out, out_size, "%s", tmp);
    }

#ifdef _WIN32
    for (char *p = out; *p; ++p) {
        if (*p == '/') {
            *p = '\\';
        }
    }
#endif
}

static void stdiofs_build_path(stdiofs_ctx_t *ctx, const char *path,
                               char *out, size_t out_size)
{
    stdiofs_normalize_path(path, ctx->base_dir, out, out_size);
}

/* Map SGL_O_* flags to a fopen mode string. Note: O_CREAT without O_TRUNC
 * cannot be expressed by a single fopen() mode; it is approximated by
 * "r+b" with a "w+b" fallback (done by the caller on open failure). */
static int stdiofs_mode_from_flags(uint32_t flags, char *mode, size_t size)
{
    uint32_t acc = flags & (SGL_O_RDONLY | SGL_O_WRONLY | SGL_O_RDWR);
    const char *base;
    const char *plus = (acc == SGL_O_RDWR) ? "+" : "";

    if (flags & SGL_O_APPEND) {
        base = "a";
    } else if ((flags & SGL_O_TRUNC) || (acc == SGL_O_WRONLY) ||
               ((flags & SGL_O_CREAT) && !(flags & SGL_O_TRUNC) && acc == SGL_O_WRONLY)) {
        base = "w";
    } else if (flags & SGL_O_CREAT) {
        base = "r";       /* handled with r+b / w+b fallback by caller */
        plus = "+";
    } else {
        base = "r";
    }

    snprintf(mode, size, "%s%sb", base, plus);
    return 0;
}

static int stdiofs_stat_impl(const char *npath, sgl_stat_t *st)
{
    stdiofs_stat_t host_st;

    if (st == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    memset(st, 0, sizeof(*st));
    if (STDIOFS_STAT(npath, &host_st) != 0) {
        return stdiofs_map_errno(errno);
    }

#ifdef _WIN32
    st->st_mode = (host_st.st_mode & _S_IFDIR) ? SGL_S_IFDIR : SGL_S_IFREG;
#else
    st->st_mode = S_ISDIR(host_st.st_mode) ? SGL_S_IFDIR : SGL_S_IFREG;
#endif
    st->st_size = (uint32_t)host_st.st_size;
    st->st_mtime = (uint32_t)POSIXFS_STAT_MTIME(&host_st);
    return SGL_FS_OK;
}

static int stdiofs_mount(void **fs, sgl_block_dev_t *dev,
                         const char *mount_point, void *fs_config)
{
    stdiofs_ctx_t *ctx;
    sgl_stdiofs_config_t *cfg = (sgl_stdiofs_config_t *)fs_config;

    STDIOFS_UNUSED(dev);
    STDIOFS_UNUSED(mount_point);

    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    *fs = NULL;

    ctx = (stdiofs_ctx_t *)sgl_malloc(sizeof(stdiofs_ctx_t));
    if (ctx == NULL) {
        return SGL_FS_NO_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->base_dir, sizeof(ctx->base_dir), "%s",
             cfg && cfg->base_dir ? cfg->base_dir : stdiofs_default_cfg.base_dir);

    *fs = ctx;
    return SGL_FS_OK;
}

static int stdiofs_unmount(void *fs, const char *mount_point)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;

    STDIOFS_UNUSED(mount_point);

    if (ctx == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    /* core closes all fds/dds of this mount point before unmounting */
    sgl_free(ctx);
    return SGL_FS_OK;
}

static int stdiofs_open(void *fs, const char *path, uint32_t flags)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char npath[STDIOFS_PATH_MAX];
    char mode[8];
    FILE *fp = NULL;
    int slot = -1;

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    stdiofs_build_path(ctx, path, npath, sizeof(npath));
    stdiofs_mode_from_flags(flags, mode, sizeof(mode));

    for (int i = 0; i < STDIOFS_MAX_FILES; ++i) {
        if (stdiofs_files[i].fp == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return SGL_FS_NO_SPACE;
    }

    if (flags & SGL_O_CREAT) {
        /* "r+b" opens existing files without truncating them, matching
         * POSIX O_CREAT semantics; fall back to "w+b" to create them. */
        char exist_mode[8];
        snprintf(exist_mode, sizeof(exist_mode), "%s", mode);
        exist_mode[0] = 'r';
        fp = fopen(npath, exist_mode);
        if (fp == NULL && !(flags & SGL_O_WRONLY)) {
            exist_mode[0] = 'w';
            fp = fopen(npath, exist_mode);
        }
    }
    if (fp == NULL) {
        fp = fopen(npath, mode);
    }
    if (fp == NULL) {
        return stdiofs_map_errno(errno);
    }

    stdiofs_files[slot].fp = fp;
    return slot;
}

static int stdiofs_close(void *fs, int fd)
{
    STDIOFS_UNUSED(fs);

    if (fd < 0 || fd >= STDIOFS_MAX_FILES || stdiofs_files[fd].fp == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    fclose(stdiofs_files[fd].fp);
    stdiofs_files[fd].fp = NULL;
    return SGL_FS_OK;
}

static int stdiofs_read(void *fs, int fd, void *buffer, uint32_t count)
{
    STDIOFS_UNUSED(fs);

    if (buffer == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (fd < 0 || fd >= STDIOFS_MAX_FILES || stdiofs_files[fd].fp == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return 0;
    }
    return (int)fread(buffer, 1, count, stdiofs_files[fd].fp);
}

static int stdiofs_write(void *fs, int fd, const void *buffer, uint32_t count)
{
    STDIOFS_UNUSED(fs);

    if (buffer == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (fd < 0 || fd >= STDIOFS_MAX_FILES || stdiofs_files[fd].fp == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return 0;
    }
    return (int)fwrite(buffer, 1, count, stdiofs_files[fd].fp);
}

static int stdiofs_seek(void *fs, int fd, int32_t offset, uint8_t whence)
{
    FILE *fp;
    int host_whence;

    STDIOFS_UNUSED(fs);

    if (fd < 0 || fd >= STDIOFS_MAX_FILES || stdiofs_files[fd].fp == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    fp = stdiofs_files[fd].fp;

    switch (whence) {
    case SGL_SEEK_SET: host_whence = SEEK_SET; break;
    case SGL_SEEK_CUR: host_whence = SEEK_CUR; break;
    case SGL_SEEK_END: host_whence = SEEK_END; break;
    default:           return SGL_FS_INVALID_ARGUMENT;
    }

    if (fseek(fp, (long)offset, host_whence) != 0) {
        return SGL_FS_ERROR;
    }
    return (int)ftell(fp);
}

static int stdiofs_opendir(void *fs, const char *path, int *dd)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char npath[STDIOFS_PATH_MAX];
    stdiofs_dir_t *dir = NULL;
    int slot = -1;

    if (ctx == NULL || path == NULL || dd == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    *dd = -1;

    for (int i = 0; i < STDIOFS_MAX_DIRS; ++i) {
        if (!stdiofs_dirs[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return SGL_FS_NO_SPACE;
    }

    stdiofs_build_path(ctx, path, npath, sizeof(npath));
    dir = &stdiofs_dirs[slot];
    memset(dir, 0, sizeof(*dir));

#ifdef _WIN32
    {
        char pattern[STDIOFS_PATH_MAX + 4];
        sgl_stat_t st;

        /* _findfirst reports both "missing dir" and "empty dir" as ENOENT;
         * disambiguate with stat first */
        if (stdiofs_stat_impl(npath, &st) != SGL_FS_OK ||
            (st.st_mode & SGL_S_IFMT) != SGL_S_IFDIR) {
            return SGL_FS_NOT_FOUND;
        }
        snprintf(pattern, sizeof(pattern), "%s\\*", npath);
        dir->handle = _findfirst(pattern, &dir->data);
        if (dir->handle == STDIOFS_FIND_INVALID) {
            if (errno == ENOENT) {
                dir->handle = STDIOFS_FIND_EXHAUSTED;   /* valid empty dir */
            } else {
                return stdiofs_map_errno(errno);
            }
        } else {
            dir->has_pending = 1;
        }
    }
#else
    dir->dirp = opendir(npath);
    if (dir->dirp == NULL) {
        return stdiofs_map_errno(errno);
    }
#endif

    dir->used = 1;
    *dd = slot;
    return SGL_FS_OK;
}

static int stdiofs_readdir(void *fs, int dd, char *name, uint32_t name_size,
                           uint32_t *type)
{
    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (dd < 0 || dd >= STDIOFS_MAX_DIRS || !stdiofs_dirs[dd].used) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    if (name == NULL || name_size == 0) {
        return SGL_FS_INVALID_ARGUMENT;
    }

#ifdef _WIN32
    {
        stdiofs_dir_t *dir = &stdiofs_dirs[dd];
        for (;;) {
            if (dir->has_pending) {
                dir->has_pending = 0;
            } else {
                if (dir->handle == STDIOFS_FIND_EXHAUSTED ||
                    dir->handle == STDIOFS_FIND_INVALID) {
                    return 0;
                }
                if (_findnext(dir->handle, &dir->data) != 0) {
                    _findclose(dir->handle);
                    dir->handle = STDIOFS_FIND_EXHAUSTED;
                    return 0;
                }
            }
            if (strcmp(dir->data.name, ".") == 0 ||
                strcmp(dir->data.name, "..") == 0) {
                continue;
            }
            snprintf(name, name_size, "%s", dir->data.name);
            if (type != NULL) {
                *type = (dir->data.attrib & _A_SUBDIR) ? SGL_S_IFDIR : SGL_S_IFREG;
            }
            return 1;
        }
    }
#else
    {
        stdiofs_dir_t *dir = &stdiofs_dirs[dd];
        struct dirent *ent;
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
                } else {
                    /* d_type unreliable on some filesystems: fall back to stat */
                    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
                    char full[STDIOFS_PATH_MAX];
                    stdiofs_stat_t host_st;
                    stdiofs_build_path(ctx, ent->d_name, full, sizeof(full));
                    if (STDIOFS_STAT(full, &host_st) == 0 &&
                        S_ISDIR(host_st.st_mode)) {
                        *type = SGL_S_IFDIR;
                    }
                }
            }
            return 1;
        }
    }
#endif
}

static int stdiofs_closedir(void *fs, int dd)
{
    STDIOFS_UNUSED(fs);

    if (dd < 0 || dd >= STDIOFS_MAX_DIRS || !stdiofs_dirs[dd].used) {
        return SGL_FS_INVALID_ARGUMENT;
    }
#ifdef _WIN32
    {
        stdiofs_dir_t *dir = &stdiofs_dirs[dd];
        if (dir->handle != STDIOFS_FIND_INVALID &&
            dir->handle != STDIOFS_FIND_EXHAUSTED) {
            _findclose(dir->handle);
        }
    }
#else
    if (stdiofs_dirs[dd].dirp != NULL) {
        closedir(stdiofs_dirs[dd].dirp);
    }
#endif
    memset(&stdiofs_dirs[dd], 0, sizeof(stdiofs_dirs[dd]));
    return SGL_FS_OK;
}

static int stdiofs_sync(void *fs)
{
    if (fs == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    for (int i = 0; i < STDIOFS_MAX_FILES; ++i) {
        if (stdiofs_files[i].fp != NULL) {
            fflush(stdiofs_files[i].fp);
        }
    }
    return SGL_FS_OK;
}

static int stdiofs_format(void *fs)
{
    /* a host pass-through has nothing to format; refuse destructively
     * wiping the base directory */
    STDIOFS_UNUSED(fs);
    return SGL_FS_ERROR;
}

static int stdiofs_remove(void *fs, const char *path)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char npath[STDIOFS_PATH_MAX];
    sgl_stat_t st;
    int ret;

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    stdiofs_build_path(ctx, path, npath, sizeof(npath));

    if (stdiofs_stat_impl(npath, &st) == SGL_FS_OK &&
        (st.st_mode & SGL_S_IFMT) == SGL_S_IFDIR) {
        ret = STDIOFS_RMDIR(npath);
    } else {
        ret = remove(npath);
    }
    return (ret == 0) ? SGL_FS_OK : stdiofs_map_errno(errno);
}

static int stdiofs_mkdir(void *fs, const char *path)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char npath[STDIOFS_PATH_MAX];

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    stdiofs_build_path(ctx, path, npath, sizeof(npath));

    if (STDIOFS_MKDIR(npath) != 0) {
        return stdiofs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int stdiofs_stat(void *fs, const char *path, sgl_stat_t *st)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char npath[STDIOFS_PATH_MAX];

    if (ctx == NULL || path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    stdiofs_build_path(ctx, path, npath, sizeof(npath));
    return stdiofs_stat_impl(npath, st);
}

static int stdiofs_rename(void *fs, const char *old_path, const char *new_path)
{
    stdiofs_ctx_t *ctx = (stdiofs_ctx_t *)fs;
    char nold[STDIOFS_PATH_MAX];
    char nnew[STDIOFS_PATH_MAX];

    if (ctx == NULL || old_path == NULL || new_path == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    stdiofs_build_path(ctx, old_path, nold, sizeof(nold));
    stdiofs_build_path(ctx, new_path, nnew, sizeof(nnew));

    if (rename(nold, nnew) != 0) {
        return stdiofs_map_errno(errno);
    }
    return SGL_FS_OK;
}

static int stdiofs_statvfs(void *fs, sgl_statvfs_t *info)
{
    if (fs == NULL || info == NULL) {
        return SGL_FS_INVALID_ARGUMENT;
    }
    /* capacity data is not meaningfully reportable for a host pass-through;
     * leaving f_blocks == 0 lets widgets skip free-space rendering */
    memset(info, 0, sizeof(*info));
    info->f_bsize = 4096;
    return SGL_FS_OK;
}

static sgl_fs_ops_t stdiofs_ops = {
    .mount    = stdiofs_mount,
    .unmount  = stdiofs_unmount,
    .open     = stdiofs_open,
    .close    = stdiofs_close,
    .read     = stdiofs_read,
    .write    = stdiofs_write,
    .seek     = stdiofs_seek,
    .opendir  = stdiofs_opendir,
    .readdir  = stdiofs_readdir,
    .closedir = stdiofs_closedir,
    .sync     = stdiofs_sync,
    .format   = stdiofs_format,
    .remove   = stdiofs_remove,
    .mkdir    = stdiofs_mkdir,
    .stat     = stdiofs_stat,
    .rename   = stdiofs_rename,
    .statvfs  = stdiofs_statvfs,
};

static sgl_fs_type_t stdiofs_fs_type = {
    .name = "stdiofs",
    .ops  = &stdiofs_ops,
};

int sgl_stdiofs_register(void)
{
    return sgl_fs_register(&stdiofs_fs_type);
}
