/* source/core/sgl_fs.c
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
#include <string.h>
#include <sgl_mm.h>

static SGL_LIST_HEAD(g_fs_type_list);
static SGL_LIST_HEAD(g_mount_list);
static sgl_fd_ctrl_t g_fd_table[SGL_MAX_FD];
static sgl_dd_ctrl_t g_dd_table[SGL_MAX_DD];

static sgl_fs_type_t* find_fs_type(const char *name)
{
    sgl_fs_type_t *pos, *tmp;
    sgl_list_for_each_entry_safe (pos, tmp, &g_fs_type_list, sgl_fs_type_t, node) {
        if (strcmp(pos->name, name) == 0) {
            return pos;
        }
    }
    return NULL;
}

int sgl_fs_register(sgl_fs_type_t *fs_type)
{
    if (!fs_type || !fs_type->name || !fs_type->ops) {
        SGL_LOG_ERROR("invalid FS type");
        return -1;
    }

    if (find_fs_type(fs_type->name) != NULL) {
        SGL_LOG_ERROR("fs type %s already registered", fs_type->name);
        return -1;
    }

    sgl_list_add_node_at_tail(&g_fs_type_list, &fs_type->node);
    return 0;
}

int sgl_fs_mount(const char *mount_point, const char *fs_name, sgl_block_dev_t *dev, void *fs_config)
{
    sgl_fs_type_t *type = find_fs_type(fs_name);
    if (!type) {
        SGL_LOG_ERROR("fs type %s not registered", fs_name);
        return -1;
    }

    sgl_mount_point_t *mp = (sgl_mount_point_t *)sgl_malloc(sizeof(sgl_mount_point_t));
    if (!mp) {
        SGL_LOG_ERROR("Failed to allocate memory for mount point");
        return -1;
    }

    mp->path = mount_point;
    mp->dev = dev;
    mp->fs_type = type;
    mp->fs_data = NULL;

    int ret = type->ops->mount(type->ops, dev, mount_point, fs_config);
    if (ret != 0) {
        sgl_free(mp);
        return ret;
    }

    sgl_list_add_node_at_tail(&g_mount_list, &mp->node);
    return 0;
}

static sgl_mount_point_t* resolve_path(const char *path, const char **rel_path)
{
    sgl_mount_point_t *best_mp = NULL;
    size_t best_len = 0;
    sgl_mount_point_t *pos, *tmp;

    if (!path || !rel_path) return NULL;

    sgl_list_for_each_entry_safe(pos, tmp, &g_mount_list, sgl_mount_point_t, node) {
        size_t len = strlen(pos->path);
        if (strncmp(path, pos->path, len) == 0) {
            if (path[len] == '\0' || path[len] == '/' || pos->path[len-1] == '/') {
                if (len > best_len) {
                    best_len = len;
                    best_mp = pos;
                }
            }
        }
    }

    if (best_mp && rel_path) {
        *rel_path = path + best_len;
        if (**rel_path == '\0') *rel_path = "/";
    }
    return best_mp;
}

int sgl_open(const char *path, uint32_t flags)
{
    const char *rel;
    sgl_mount_point_t *mp = resolve_path(path, &rel);
    if (!mp) {
        SGL_LOG_ERROR("No mount point found for path: %s", path);
        return -1;
    }

    int local_fd = mp->fs_type->ops->open(mp->fs_type->ops, rel, flags);
    if (local_fd < 0) {
        SGL_LOG_ERROR("failed to open file: %s", path);
        return local_fd;
    }

    for (int i = 0; i < SGL_MAX_FD; i++) {
        if (!g_fd_table[i].used) {
            g_fd_table[i].used = 1;
            g_fd_table[i].mp = mp;
            g_fd_table[i].local_fd = local_fd;
            return i;
        }
    }

    mp->fs_type->ops->close(mp->fs_type->ops, local_fd);
    SGL_LOG_ERROR("FD table full, max=%d", SGL_MAX_FD);
    return -1;
}

int sgl_close(int fd) 
{
    int ret;
    sgl_fd_ctrl_t *ctrl;
    if (fd < 0 || fd >= SGL_MAX_FD || !g_fd_table[fd].used) {
        SGL_LOG_ERROR("invalid file descriptor: %d", fd);
        return -1;
    }

    ctrl = &g_fd_table[fd];
    ret = ctrl->mp->fs_type->ops->close(ctrl->mp->fs_type->ops, ctrl->local_fd);

    ctrl->used = 0;
    ctrl->mp = NULL;
    ctrl->local_fd = -1;

    return ret;
}

int sgl_read(int fd, void *buffer, uint32_t count) 
{
    if (fd < 0 || fd >= SGL_MAX_FD || !g_fd_table[fd].used) {
        SGL_LOG_ERROR("invalid file descriptor: %d", fd);
        return -1;
    }

    sgl_fd_ctrl_t *ctrl = &g_fd_table[fd];
    return ctrl->mp->fs_type->ops->read(
        ctrl->mp->fs_type->ops, 
        ctrl->local_fd, 
        buffer, 
        count
    );
}

int sgl_write(int fd, const void *buffer, uint32_t count) 
{
    if (fd < 0 || fd >= SGL_MAX_FD || !g_fd_table[fd].used) {
        SGL_LOG_ERROR("invalid file descriptor: %d", fd);
        return -1;
    }

    sgl_fd_ctrl_t *ctrl = &g_fd_table[fd];
    return ctrl->mp->fs_type->ops->write( ctrl->mp->fs_type->ops, 
                                          ctrl->local_fd, 
                                          buffer, 
                                          count
                                        );
}

int sgl_stat(const char *path, sgl_stat_t *st)
{
    const char *rel;
    sgl_mount_point_t *mp = resolve_path(path, &rel);
    if (!mp) {
        SGL_LOG_ERROR("no mount point found for path: %s", path);
        return -1;
    }
    return mp->fs_type->ops->stat(mp->fs_type->ops, rel, st);
}

int sgl_opendir(const char *path, int *dd)
{
    const char *rel;
    sgl_mount_point_t *mp = resolve_path(path, &rel);
    if (!mp) {
        SGL_LOG_ERROR("no mount point found for path: %s", path);
        return -1;
    }
    if (!dd) return -1;

    int local_dd = -1;
    int ret = mp->fs_type->ops->opendir(mp->fs_type->ops, rel, &local_dd);
    if (ret != 0 || local_dd < 0) {
        SGL_LOG_ERROR("failed to opendir: %s", path);
        return ret;
    }

    for (int i = 0; i < SGL_MAX_DD; i++) {
        if (!g_dd_table[i].used) {
            g_dd_table[i].used = 1;
            g_dd_table[i].mp = mp;
            g_dd_table[i].local_dd = (int16_t)local_dd;
            *dd = i;
            return 0;
        }
    }

    mp->fs_type->ops->closedir(mp->fs_type->ops, local_dd);
    SGL_LOG_ERROR("directory descriptor table full, max=%d", SGL_MAX_DD);
    return -1;
}

int sgl_readdir(int dd, char *name, uint32_t name_size, uint32_t *type)
{
    if (dd < 0 || dd >= SGL_MAX_DD || !g_dd_table[dd].used) {
        SGL_LOG_ERROR("invalid directory descriptor: %d", dd);
        return -1;
    }

    sgl_dd_ctrl_t *ctrl = &g_dd_table[dd];
    return ctrl->mp->fs_type->ops->readdir(
        ctrl->mp->fs_type->ops,
        ctrl->local_dd,
        name,
        name_size,
        type
    );
}

int sgl_closedir(int dd)
{
    if (dd < 0 || dd >= SGL_MAX_DD || !g_dd_table[dd].used) {
        SGL_LOG_ERROR("invalid directory descriptor: %d", dd);
        return -1;
    }

    sgl_dd_ctrl_t *ctrl = &g_dd_table[dd];
    int ret = ctrl->mp->fs_type->ops->closedir(ctrl->mp->fs_type->ops, ctrl->local_dd);

    ctrl->used = 0;
    ctrl->mp = NULL;
    ctrl->local_dd = -1;

    return ret;
}

int sgl_unmount(const char *mount_point)
{
    int ret;
    sgl_mount_point_t *pos, *tmp;
    sgl_list_for_each_entry_safe(pos, tmp, &g_mount_list, sgl_mount_point_t, node) {
        if (strcmp(pos->path, mount_point) == 0) {
            for (int i = 0; i < SGL_MAX_FD; i++) {
                if (g_fd_table[i].used && g_fd_table[i].mp == pos) {
                    SGL_LOG_ERROR("cannot unmount: fd %d still open on %s", i, mount_point);
                    return -1;
                }
            }

            ret = pos->fs_type->ops->unmount(pos->fs_type->ops, mount_point);
            sgl_list_del_node(&pos->node);
            sgl_free(pos);
            return ret;
        }
    }
    SGL_LOG_ERROR("mount point not found: %s", mount_point);
    return -1;
}

int sgl_sync(const char *path)
{
    if (path) {
        const char *rel;
        sgl_mount_point_t *mp = resolve_path(path, &rel);
        if (!mp) return -1;
        return mp->fs_type->ops->sync(mp->fs_type->ops);
    }
    else {
        sgl_mount_point_t *pos, *tmp;
        sgl_list_for_each_entry_safe(pos, tmp, &g_mount_list, sgl_mount_point_t, node) {
            pos->fs_type->ops->sync(pos->fs_type->ops);
        }
        return 0;
    }
}

int sgl_format(const char *mount_point, void *fs_config)
{
    sgl_mount_point_t *pos, *tmp;
    SGL_UNUSED(fs_config);
    sgl_list_for_each_entry_safe(pos, tmp, &g_mount_list, sgl_mount_point_t, node) {
        if (strcmp(pos->path, mount_point) == 0) {
            return pos->fs_type->ops->format(pos->fs_type->ops);
        }
    }
    SGL_LOG_ERROR("mount point not found for format: %s", mount_point);
    return -1;
}

int sgl_remove(const char *path)
{
    const char *rel;
    sgl_mount_point_t *mp = resolve_path(path, &rel);
    if (!mp) {
        SGL_LOG_ERROR("No mount point found for remove: %s", path);
        return -1;
    }
    return mp->fs_type->ops->remove(mp->fs_type->ops, rel);
}

int sgl_mkdir(const char *path)
{
    const char *rel;
    sgl_mount_point_t *mp = resolve_path(path, &rel);
    if (!mp) {
        SGL_LOG_ERROR("no mount point found for mkdir: %s", path);
        return -1;
    }
    return mp->fs_type->ops->mkdir(mp->fs_type->ops, rel);
}

int sgl_rename(const char *old_path, const char *new_path)
{
    const char *old_rel, *new_rel;
    sgl_mount_point_t *old_mp = resolve_path(old_path, &old_rel);
    sgl_mount_point_t *new_mp = resolve_path(new_path, &new_rel);
    
    if (!old_mp || !new_mp) {
        SGL_LOG_ERROR("mount point not found for rename");
        return -1;
    }

    if (old_mp != new_mp) {
        SGL_LOG_ERROR("cross-fs rename not supported");
        return -1;
    }

    return old_mp->fs_type->ops->rename(old_mp->fs_type->ops, old_rel, new_rel);
}
