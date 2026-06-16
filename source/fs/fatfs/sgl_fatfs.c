/* source/fs/fatfs/sgl_fatfs.c
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
#include "sgl_fatfs.h"
#include <string.h>
#include <sgl_mm.h>

/* ===================== Constants ===================== */
#define FAT_MAX_OPEN_FILES  8
#define FAT_MAX_OPEN_DIRS   4
#define FAT_MAX_VOLS        4

#define FAT_TYPE_12  12
#define FAT_TYPE_16  16
#define FAT_TYPE_32  32

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  (FAT_ATTR_READ_ONLY|FAT_ATTR_HIDDEN|FAT_ATTR_SYSTEM|FAT_ATTR_VOLUME_ID)

#define FAT_ENTRY_FREE      0xE5
#define FAT_ENTRY_END       0x00
#define FAT_DIR_ENTRY_SIZE  32

/* End-of-chain markers */
#define FAT12_EOC_MIN  0xFF8
#define FAT16_EOC_MIN  0xFFF8
#define FAT32_EOC_MIN  0x0FFFFFF8

/* Open flags */
#ifndef SGL_O_RDONLY
#define SGL_O_RDONLY  0x00
#define SGL_O_WRONLY  0x01
#define SGL_O_RDWR    0x02
#define SGL_O_CREAT   0x10
#define SGL_O_TRUNC   0x20
#define SGL_O_APPEND  0x40
#endif

/* ===================== Error codes ===================== */
enum {
    FAT_OK = 0,
    FAT_ERR_IO = -1,
    FAT_ERR_NOT_FOUND = -2,
    FAT_ERR_INVALID = -3,
    FAT_ERR_DENIED = -4,
    FAT_ERR_EXIST = -5,
    FAT_ERR_NO_SPACE = -6,
    FAT_ERR_TOO_MANY_OPEN = -7,
    FAT_ERR_NOT_DIR = -8,
    FAT_ERR_NOT_EMPTY = -9,
    FAT_ERR_NO_MEM = -10,
};

/* ===================== Data Structures ===================== */

/* 8.3 short name entry on disk (32 bytes) */
typedef struct {
    uint8_t  name[11];     /* 8.3 filename, space-padded */
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct {
    uint8_t  used;
    uint32_t dir_start_cluster;  /* first cluster of parent dir (0=root) */
    uint32_t entry_sector;       /* sector containing the dir entry */
    uint16_t entry_offset;       /* byte offset within sector */
    uint32_t start_cluster;      /* first data cluster of file */
    uint32_t cur_cluster;        /* current cluster for r/w */
    uint32_t cur_pos;            /* current byte position */
    uint32_t file_size;
    uint8_t  flags;              /* SGL_O_xxx */
    uint8_t  dirty;              /* entry metadata needs flush */
} fat_file_t;

typedef struct {
    uint8_t  used;
    uint32_t start_cluster;      /* first cluster of the directory */
    uint32_t cur_cluster;        /* current cluster being read */
    uint32_t entry_index;        /* next entry index to read */
    uint8_t  finished;           /* end of directory reached */
} fat_dir_t;

typedef struct {
    sgl_block_dev_t *dev;
    uint8_t  vol_id;

    /* BPB derived values */
    uint16_t sector_size;
    uint8_t  cluster_size;       /* sectors per cluster */
    uint8_t  fat_type;           /* 12, 16, or 32 */
    uint8_t  num_fats;
    uint32_t fat_start;          /* first sector of FAT */
    uint32_t root_dir_start;     /* first sector of root dir (FAT12/16) */
    uint16_t root_entry_count;   /* root dir entries (FAT12/16) */
    uint32_t data_start;         /* first sector of data region */
    uint32_t total_clusters;
    uint32_t total_sectors;
    uint32_t fat_sectors;        /* sectors per FAT */
    uint32_t root_dir_sectors;   /* root dir sectors (FAT12/16) */
    uint32_t fat32_root_cluster; /* root dir cluster (FAT32) */

    /* Sector cache (single buffer) */
    uint8_t *sec_buf;
    uint32_t sec_buf_num;        /* sector number in buffer, 0xFFFFFFFF=invalid */
    uint8_t  sec_buf_dirty;

    fat_file_t files[FAT_MAX_OPEN_FILES];
    fat_dir_t  dirs[FAT_MAX_OPEN_DIRS];
} fat_ctx_t;

/* Global volume -> block device mapping */
static sgl_block_dev_t *g_vol_dev_map[FAT_MAX_VOLS];

/* ===================== Low-level helpers ===================== */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void wr16(uint8_t *p, uint16_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; }
static void wr32(uint8_t *p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

static int fat_read_sector(fat_ctx_t *ctx, uint32_t sector, uint8_t *buf)
{
    return ctx->dev->read_sectors(ctx->dev, sector, buf, 1);
}

static int fat_write_sector(fat_ctx_t *ctx, uint32_t sector, const uint8_t *buf)
{
    return ctx->dev->write_sectors(ctx->dev, sector, buf, 1);
}

/* Cached sector read */
static int fat_cache_read(fat_ctx_t *ctx, uint32_t sector)
{
    if (ctx->sec_buf_num == sector) return FAT_OK;
    if (ctx->sec_buf_dirty && ctx->sec_buf_num != 0xFFFFFFFF) {
        if (fat_write_sector(ctx, ctx->sec_buf_num, ctx->sec_buf) != 0)
            return FAT_ERR_IO;
    }
    if (fat_read_sector(ctx, sector, ctx->sec_buf) != 0)
        return FAT_ERR_IO;
    ctx->sec_buf_num = sector;
    ctx->sec_buf_dirty = 0;
    return FAT_OK;
}

/* Mark cache dirty */
static void fat_cache_dirty(fat_ctx_t *ctx)
{
    ctx->sec_buf_dirty = 1;
}

/* Flush cache */
static int fat_cache_flush(fat_ctx_t *ctx)
{
    if (ctx->sec_buf_dirty && ctx->sec_buf_num != 0xFFFFFFFF) {
        if (fat_write_sector(ctx, ctx->sec_buf_num, ctx->sec_buf) != 0)
            return FAT_ERR_IO;
        ctx->sec_buf_dirty = 0;
    }
    return FAT_OK;
}

/* Invalidate cache */
static void fat_cache_invalidate(fat_ctx_t *ctx)
{
    if (ctx->sec_buf_dirty && ctx->sec_buf_num != 0xFFFFFFFF) {
        fat_write_sector(ctx, ctx->sec_buf_num, ctx->sec_buf);
    }
    ctx->sec_buf_num = 0xFFFFFFFF;
    ctx->sec_buf_dirty = 0;
}

/* ===================== Cluster / FAT operations ===================== */

static uint32_t cluster_to_sector(fat_ctx_t *ctx, uint32_t cluster)
{
    if (ctx->fat_type == FAT_TYPE_32 && cluster == 0) {
        cluster = ctx->fat32_root_cluster;
    }
    return ctx->data_start + (uint32_t)(cluster - 2) * ctx->cluster_size;
}

static int is_eoc(fat_ctx_t *ctx, uint32_t entry)
{
    if (ctx->fat_type == FAT_TYPE_12) return entry >= FAT12_EOC_MIN;
    if (ctx->fat_type == FAT_TYPE_16) return entry >= FAT16_EOC_MIN;
    return entry >= FAT32_EOC_MIN;
}

/* Read FAT entry for a given cluster */
static uint32_t fat_get_entry(fat_ctx_t *ctx, uint32_t cluster)
{
    uint32_t offset, ent_offset, sec;
    int ret;

    if (ctx->fat_type == FAT_TYPE_12) {
        offset = cluster + (cluster / 2);  /* 1.5 bytes per entry */
    } else if (ctx->fat_type == FAT_TYPE_16) {
        offset = cluster * 2;
    } else {
        offset = cluster * 4;
    }

    sec = ctx->fat_start + offset / ctx->sector_size;
    ent_offset = offset % ctx->sector_size;

    ret = fat_cache_read(ctx, sec);
    if (ret != FAT_OK) return 0xFFFFFFFF;

    if (ctx->fat_type == FAT_TYPE_12) {
        uint16_t val;
        if ((uint16_t)ent_offset == ctx->sector_size - 1) {
            /* Entry spans two sectors */
            uint8_t b0 = ctx->sec_buf[ent_offset];
            fat_cache_read(ctx, sec + 1);
            uint8_t b1 = ctx->sec_buf[0];
            val = (uint16_t)b0 | ((uint16_t)b1 << 8);
        } else {
            val = rd16(&ctx->sec_buf[ent_offset]);
        }
        if (cluster & 1) val >>= 4;
        else val &= 0x0FFF;
        return val;
    } else if (ctx->fat_type == FAT_TYPE_16) {
        return rd16(&ctx->sec_buf[ent_offset]);
    } else {
        return rd32(&ctx->sec_buf[ent_offset]) & 0x0FFFFFFF;
    }
}

/* Write FAT entry for a given cluster (writes to all FAT copies) */
static int fat_set_entry(fat_ctx_t *ctx, uint32_t cluster, uint32_t value)
{
    for (uint8_t f = 0; f < ctx->num_fats; f++) {
        uint32_t fat_base = ctx->fat_start + (uint32_t)f * ctx->fat_sectors;
        uint32_t offset, ent_offset, sec;

        if (ctx->fat_type == FAT_TYPE_12) {
            offset = cluster + (cluster / 2);
        } else if (ctx->fat_type == FAT_TYPE_16) {
            offset = cluster * 2;
        } else {
            offset = cluster * 4;
        }

        sec = fat_base + offset / ctx->sector_size;
        ent_offset = offset % ctx->sector_size;

        if (fat_cache_read(ctx, sec) != FAT_OK) return FAT_ERR_IO;

        if (ctx->fat_type == FAT_TYPE_12) {
            uint16_t val = rd16(&ctx->sec_buf[ent_offset]);
            if (cluster & 1) {
                val = (val & 0x000F) | ((value & 0x0FFF) << 4);
            } else {
                val = (val & 0xF000) | (value & 0x0FFF);
            }
            wr16(&ctx->sec_buf[ent_offset], val);
            fat_cache_dirty(ctx);
            /* Handle sector-spanning case */
            if (ent_offset == (uint32_t)(ctx->sector_size - 1)) {
                fat_cache_flush(ctx);
                fat_cache_read(ctx, sec + 1);
                ctx->sec_buf[0] = (uint8_t)((val >> 8) & 0xFF);
                fat_cache_dirty(ctx);
            }
        } else if (ctx->fat_type == FAT_TYPE_16) {
            wr16(&ctx->sec_buf[ent_offset], (uint16_t)(value & 0xFFFF));
            fat_cache_dirty(ctx);
        } else {
            uint32_t old = rd32(&ctx->sec_buf[ent_offset]);
            wr32(&ctx->sec_buf[ent_offset], (old & 0xF0000000) | (value & 0x0FFFFFFF));
            fat_cache_dirty(ctx);
        }
        fat_cache_flush(ctx);
    }
    return FAT_OK;
}

/* Allocate a free cluster, returns cluster number or 0 on failure */
static uint32_t fat_alloc_cluster(fat_ctx_t *ctx)
{
    for (uint32_t c = 2; c < ctx->total_clusters + 2; c++) {
        uint32_t val = fat_get_entry(ctx, c);
        if (val == 0) {
            /* Mark as end of chain */
            uint32_t eoc = (ctx->fat_type == FAT_TYPE_12) ? 0xFFF :
                           (ctx->fat_type == FAT_TYPE_16) ? 0xFFFF : 0x0FFFFFFF;
            fat_set_entry(ctx, c, eoc);
            return c;
        }
    }
    return 0;
}

/* Free a cluster chain starting from 'start' */
static int fat_free_chain(fat_ctx_t *ctx, uint32_t start)
{
    if (start < 2) return FAT_OK;
    uint32_t cur = start;
    while (cur >= 2 && !is_eoc(ctx, cur)) {
        uint32_t next = fat_get_entry(ctx, cur);
        fat_set_entry(ctx, cur, 0);
        cur = next;
    }
    if (cur >= 2) fat_set_entry(ctx, cur, 0); /* free last cluster */
    return FAT_OK;
}

/* Get the Nth cluster in a chain (0-based). Returns 0 on failure */
static uint32_t fat_chain_get(fat_ctx_t *ctx, uint32_t start, uint32_t index)
{
    uint32_t cur = start;
    for (uint32_t i = 0; i < index; i++) {
        if (cur < 2 || is_eoc(ctx, cur)) return 0;
        cur = fat_get_entry(ctx, cur);
    }
    return (cur >= 2 && !is_eoc(ctx, cur)) ? cur : (index == 0 ? start : 0);
}

/* ===================== 8.3 Name Handling ===================== */

/* Convert a filename component to 8.3 format (space-padded, uppercase) */
static void name_to_83(const char *name, uint8_t out[11])
{
    memset(out, ' ', 11);
    int i = 0, dot = -1;
    /* Find dot position */
    for (int j = 0; name[j]; j++) {
        if (name[j] == '.') { dot = j; break; }
    }

    /* Copy base name (up to 8 chars before dot or end) */
    int base_len = (dot >= 0) ? dot : (int)strlen(name);
    if (base_len > 8) base_len = 8;
    for (i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i] = (uint8_t)c;
    }

    /* Copy extension (up to 3 chars after dot) */
    if (dot >= 0) {
        int ext_len = (int)strlen(name + dot + 1);
        if (ext_len > 3) ext_len = 3;
        for (i = 0; i < ext_len; i++) {
            char c = name[dot + 1 + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[8 + i] = (uint8_t)c;
        }
    }
}

/* Convert 8.3 dir entry name to null-terminated string */
static void name_from_83(const uint8_t entry[11], char *out, uint32_t out_size)
{
    uint32_t pos = 0;
    /* Copy base name, trim trailing spaces */
    int base_end = 7;
    while (base_end >= 0 && entry[base_end] == ' ') base_end--;
    for (int i = 0; i <= base_end && pos < out_size - 1; i++) {
        out[pos++] = (char)entry[i];
    }
    /* Copy extension if present */
    int ext_end = 10;
    while (ext_end >= 8 && entry[ext_end] == ' ') ext_end--;
    if (ext_end >= 8 && pos < out_size - 2) {
        out[pos++] = '.';
        for (int i = 8; i <= ext_end && pos < out_size - 1; i++) {
            out[pos++] = (char)entry[i];
        }
    }
    out[pos] = '\0';
}

/* ===================== Path / Directory Lookup ===================== */

/* Get root directory start cluster */
static uint32_t root_cluster(fat_ctx_t *ctx)
{
    if (ctx->fat_type == FAT_TYPE_32) return ctx->fat32_root_cluster;
    return 0; /* FAT12/16: root is in fixed area */
}

/* Read a directory entry at a given index within a directory.
   dir_cluster=0 means root dir (FAT12/16 fixed area).
   Returns FAT_OK, or FAT_ERR_NOT_FOUND when end reached */
static int fat_read_dir_entry(fat_ctx_t *ctx, uint32_t dir_cluster,
                              uint32_t index, fat_dir_entry_t *entry,
                              uint32_t *out_sector, uint16_t *out_offset)
{
    uint32_t byte_offset = index * FAT_DIR_ENTRY_SIZE;
    uint32_t sector_in_dir = byte_offset / ctx->sector_size;
    uint16_t offset_in_sec = (uint16_t)(byte_offset % ctx->sector_size);
    uint32_t sector;

    if (dir_cluster == 0) {
        /* FAT12/16 root directory: fixed location, limited entries */
        uint32_t max_entries = (uint32_t)ctx->root_entry_count;
        if (index >= max_entries) return FAT_ERR_NOT_FOUND;
        sector = ctx->root_dir_start + sector_in_dir;
        if (sector >= ctx->root_dir_start + ctx->root_dir_sectors)
            return FAT_ERR_NOT_FOUND;
    } else {
        /* Cluster-based directory */
        uint32_t cluster_index = sector_in_dir / ctx->cluster_size;
        uint32_t sec_in_cluster = sector_in_dir % ctx->cluster_size;

        uint32_t clust = fat_chain_get(ctx, dir_cluster, cluster_index);
        if (clust == 0) return FAT_ERR_NOT_FOUND;
        sector = cluster_to_sector(ctx, clust) + sec_in_cluster;
    }

    if (fat_cache_read(ctx, sector) != FAT_OK) return FAT_ERR_IO;
    memcpy(entry, &ctx->sec_buf[offset_in_sec], FAT_DIR_ENTRY_SIZE);

    if (out_sector) *out_sector = sector;
    if (out_offset) *out_offset = offset_in_sec;

    if (entry->name[0] == FAT_ENTRY_END) return FAT_ERR_NOT_FOUND;
    return FAT_OK;
}

/* Write a directory entry at a given sector/offset */
static int fat_write_dir_entry(fat_ctx_t *ctx, uint32_t sector, uint16_t offset,
                               const fat_dir_entry_t *entry)
{
    if (fat_cache_read(ctx, sector) != FAT_OK) return FAT_ERR_IO;
    memcpy(&ctx->sec_buf[offset], entry, FAT_DIR_ENTRY_SIZE);
    fat_cache_dirty(ctx);
    return fat_cache_flush(ctx);
}

/* Search a directory for a given name component.
   Returns FAT_OK if found, FAT_ERR_NOT_FOUND if not */
static int fat_find_in_dir(fat_ctx_t *ctx, uint32_t dir_cluster,
                           const char *name, fat_dir_entry_t *entry,
                           uint32_t *out_sector, uint16_t *out_offset)
{
    uint8_t target[11];
    name_to_83(name, target);

    for (uint32_t i = 0; ; i++) {
        fat_dir_entry_t e;
        uint32_t sec; uint16_t off;
        int ret = fat_read_dir_entry(ctx, dir_cluster, i, &e, &sec, &off);
        if (ret != FAT_OK) return ret;
        if (e.name[0] == FAT_ENTRY_FREE) continue;
        if (e.attr & FAT_ATTR_LONG_NAME) continue;
        if (e.attr & FAT_ATTR_VOLUME_ID) continue;
        if (memcmp(e.name, target, 11) == 0) {
            *entry = e;
            if (out_sector) *out_sector = sec;
            if (out_offset) *out_offset = off;
            return FAT_OK;
        }
    }
}

/* Find a free directory entry slot in a directory */
static int fat_find_free_entry(fat_ctx_t *ctx, uint32_t dir_cluster,
                               uint32_t *out_sector, uint16_t *out_offset)
{
    if (dir_cluster == 0) {
        /* FAT12/16 root: limited entries, cannot grow */
        for (uint32_t i = 0; i < ctx->root_entry_count; i++) {
            fat_dir_entry_t e;
            uint32_t sec; uint16_t off;
            fat_read_dir_entry(ctx, 0, i, &e, &sec, &off);
            if (e.name[0] == FAT_ENTRY_FREE || e.name[0] == FAT_ENTRY_END) {
                *out_sector = sec;
                *out_offset = off;
                return FAT_OK;
            }
        }
        return FAT_ERR_NO_SPACE;
    }

    /* Cluster-based directory: search and extend if needed */
    for (uint32_t i = 0; ; i++) {
        fat_dir_entry_t e;
        uint32_t sec; uint16_t off;
        int ret = fat_read_dir_entry(ctx, dir_cluster, i, &e, &sec, &off);
        if (ret == FAT_ERR_NOT_FOUND) {
            /* Need to extend directory by one cluster */
            uint32_t new_clust = fat_alloc_cluster(ctx);
            if (new_clust == 0) return FAT_ERR_NO_SPACE;
            /* Zero the new cluster */
            uint32_t base = cluster_to_sector(ctx, new_clust);
            memset(ctx->sec_buf, 0, ctx->sector_size);
            for (uint8_t s = 0; s < ctx->cluster_size; s++) {
                fat_write_sector(ctx, base + s, ctx->sec_buf);
            }
            /* Link to end of chain */
            uint32_t cur = dir_cluster;
            while (!is_eoc(ctx, fat_get_entry(ctx, cur))) {
                cur = fat_get_entry(ctx, cur);
            }
            uint32_t eoc = (ctx->fat_type == FAT_TYPE_12) ? 0xFFF :
                           (ctx->fat_type == FAT_TYPE_16) ? 0xFFFF : 0x0FFFFFFF;
            fat_set_entry(ctx, cur, new_clust);
            fat_set_entry(ctx, new_clust, eoc);
            /* Retry */
            return fat_find_free_entry(ctx, dir_cluster, out_sector, out_offset);
        }
        if (ret != FAT_OK) return ret;
        if (e.name[0] == FAT_ENTRY_FREE || e.name[0] == FAT_ENTRY_END) {
            *out_sector = sec;
            *out_offset = off;
            return FAT_OK;
        }
    }
}

/* Resolve a path to its parent directory cluster and the final name component.
   Returns FAT_OK on success. *parent_cluster = 0 for FAT12/16 root.
   final_name points into the original path string. */
static int fat_resolve_path(fat_ctx_t *ctx, const char *path,
                            uint32_t *parent_cluster, const char **final_name)
{
    /* Skip leading slash */
    while (*path == '/') path++;
    if (*path == '\0') {
        /* Root directory itself */
        *parent_cluster = root_cluster(ctx);
        *final_name = NULL;
        return FAT_OK;
    }

    uint32_t cur_dir = root_cluster(ctx);
    char component[13]; /* max 8.3 name + null */

    while (*path) {
        /* Extract next component */
        int i = 0;
        while (*path && *path != '/' && i < 12) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        while (*path == '/') path++;

        if (*path == '\0') {
            /* This is the final component */
            *parent_cluster = cur_dir;
            *final_name = path - i; /* point back to start of component */
            return FAT_OK;
        }

        /* Intermediate component: must be a directory */
        fat_dir_entry_t entry;
        int ret = fat_find_in_dir(ctx, cur_dir, component, &entry, NULL, NULL);
        if (ret != FAT_OK) return FAT_ERR_NOT_FOUND;
        if (!(entry.attr & FAT_ATTR_DIRECTORY)) return FAT_ERR_NOT_DIR;

        cur_dir = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (cur_dir == 0) cur_dir = root_cluster(ctx);
    }

    *parent_cluster = cur_dir;
    *final_name = NULL;
    return FAT_OK;
}

/* Check if a directory is empty (only . and .. or nothing) */
static int fat_dir_is_empty(fat_ctx_t *ctx, uint32_t dir_cluster)
{
    for (uint32_t i = 0; ; i++) {
        fat_dir_entry_t e;
        int ret = fat_read_dir_entry(ctx, dir_cluster, i, &e, NULL, NULL);
        if (ret != FAT_OK) return 1; /* end of dir = empty */
        if (e.name[0] == FAT_ENTRY_FREE) continue;
        if (e.attr & FAT_ATTR_LONG_NAME) continue;
        /* Check for . and .. */
        if (e.name[0] == '.' && (e.name[1] == ' ' || (e.name[1] == '.' && e.name[2] == ' ')))
            continue;
        return 0; /* Found a real entry */
    }
}

/* Create a new directory entry in parent_dir. Returns sector/offset of new entry */
static int fat_create_entry(fat_ctx_t *ctx, uint32_t parent_dir,
                            const char *name, uint8_t attr,
                            uint32_t start_cluster, uint32_t file_size,
                            uint32_t *out_sector, uint16_t *out_offset)
{
    uint32_t sec; uint16_t off;
    int ret = fat_find_free_entry(ctx, parent_dir, &sec, &off);
    if (ret != FAT_OK) return ret;

    fat_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    name_to_83(name, entry.name);
    entry.attr = attr;
    entry.fst_clus_lo = (uint16_t)(start_cluster & 0xFFFF);
    entry.fst_clus_hi = (uint16_t)((start_cluster >> 16) & 0xFFFF);
    entry.file_size = file_size;

    ret = fat_write_dir_entry(ctx, sec, off, &entry);
    if (ret == FAT_OK) {
        if (out_sector) *out_sector = sec;
        if (out_offset) *out_offset = off;
    }
    return ret;
}

/* ===================== VFS Operations ===================== */

static int fatfs_mount(void **fs, sgl_block_dev_t *dev,
                       const char *mount_point, void *fs_config)
{
    (void)mount_point;
    sgl_fatfs_config_t *cfg = (sgl_fatfs_config_t *)fs_config;
    if (!cfg || !dev) return FAT_ERR_INVALID;
    if (cfg->vol_id >= FAT_MAX_VOLS) return FAT_ERR_INVALID;

    if (dev->init && dev->init(dev) != 0) return FAT_ERR_IO;

    fat_ctx_t *ctx = (fat_ctx_t *)sgl_malloc(sizeof(fat_ctx_t));
    if (!ctx) return FAT_ERR_NO_MEM;
    memset(ctx, 0, sizeof(fat_ctx_t));

    ctx->dev = dev;
    ctx->vol_id = cfg->vol_id;
    ctx->sec_buf_num = 0xFFFFFFFF;
    g_vol_dev_map[cfg->vol_id] = dev;

    ctx->sec_buf = (uint8_t *)sgl_malloc(512);
    if (!ctx->sec_buf) { sgl_free(ctx); return FAT_ERR_NO_MEM; }

    if (fat_read_sector(ctx, 0, ctx->sec_buf) != 0) {
        sgl_free(ctx->sec_buf); sgl_free(ctx); return FAT_ERR_IO;
    }

    uint8_t *bpb = ctx->sec_buf;
    if (bpb[0] != 0xEB && bpb[0] != 0xE9 && bpb[0] != 0xE8) {
        sgl_free(ctx->sec_buf); sgl_free(ctx); return FAT_ERR_INVALID;
    }
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) {
        sgl_free(ctx->sec_buf); sgl_free(ctx); return FAT_ERR_INVALID;
    }

    ctx->sector_size = rd16(&bpb[11]);
    ctx->cluster_size = bpb[13];
    ctx->num_fats = bpb[16];
    ctx->root_entry_count = rd16(&bpb[17]);
    ctx->total_sectors = rd16(&bpb[19]);
    if (ctx->total_sectors == 0) ctx->total_sectors = rd32(&bpb[32]);
    ctx->fat_sectors = rd16(&bpb[22]);
    if (ctx->fat_sectors == 0) ctx->fat_sectors = rd32(&bpb[36]);

    if (ctx->sector_size != 512) {
        sgl_free(ctx->sec_buf);
        ctx->sec_buf = (uint8_t *)sgl_malloc(ctx->sector_size);
        if (!ctx->sec_buf) { sgl_free(ctx); return FAT_ERR_NO_MEM; }
        if (fat_read_sector(ctx, 0, ctx->sec_buf) != 0) {
            sgl_free(ctx->sec_buf); sgl_free(ctx); return FAT_ERR_IO;
        }
        bpb = ctx->sec_buf;
    }

    uint16_t rsvd_sectors = rd16(&bpb[14]);
    ctx->fat_start = rsvd_sectors;
    ctx->root_dir_sectors = ((uint32_t)ctx->root_entry_count * 32 + ctx->sector_size - 1) / ctx->sector_size;
    ctx->root_dir_start = ctx->fat_start + (uint32_t)ctx->num_fats * ctx->fat_sectors;
    ctx->data_start = ctx->root_dir_start + ctx->root_dir_sectors;
    uint32_t data_sectors = ctx->total_sectors - ctx->data_start;
    ctx->total_clusters = data_sectors / ctx->cluster_size;

    if (ctx->total_clusters < 4085)      ctx->fat_type = FAT_TYPE_12;
    else if (ctx->total_clusters < 65525) ctx->fat_type = FAT_TYPE_16;
    else { ctx->fat_type = FAT_TYPE_32; ctx->fat32_root_cluster = rd32(&bpb[44]); }

    *fs = ctx;
    return FAT_OK;
}

static int fatfs_unmount(void *fs, const char *mount_point)
{
    (void)mount_point;
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (!ctx) return FAT_ERR_INVALID;
    fat_cache_invalidate(ctx);
    g_vol_dev_map[ctx->vol_id] = NULL;
    sgl_free(ctx->sec_buf);
    sgl_free(ctx);
    return FAT_OK;
}

static int fatfs_open(void *fs, const char *path, uint32_t flags)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    int slot = -1;
    for (int i = 0; i < FAT_MAX_OPEN_FILES; i++)
        if (!ctx->files[i].used) { slot = i; break; }
    if (slot < 0) return FAT_ERR_TOO_MANY_OPEN;

    uint32_t parent_dir;
    const char *fname;
    int ret = fat_resolve_path(ctx, path, &parent_dir, &fname);
    if (ret != FAT_OK) return ret;
    if (fname == NULL) return FAT_ERR_INVALID;

    fat_dir_entry_t entry;
    uint32_t entry_sec; uint16_t entry_off;
    int found = fat_find_in_dir(ctx, parent_dir, fname, &entry, &entry_sec, &entry_off);

    if (found == FAT_OK) {
        if (entry.attr & FAT_ATTR_DIRECTORY) return FAT_ERR_NOT_DIR;
        uint32_t start = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (flags & SGL_O_TRUNC) {
            if (start >= 2) fat_free_chain(ctx, start);
            start = 0;
            entry.fst_clus_lo = 0; entry.fst_clus_hi = 0; entry.file_size = 0;
            fat_write_dir_entry(ctx, entry_sec, entry_off, &entry);
        }
        fat_file_t *f = &ctx->files[slot];
        memset(f, 0, sizeof(fat_file_t));
        f->used = 1; f->dir_start_cluster = parent_dir;
        f->entry_sector = entry_sec; f->entry_offset = entry_off;
        f->start_cluster = start; f->cur_cluster = start;
        f->file_size = entry.file_size; f->flags = (uint8_t)(flags & 0xFF);
        if (flags & SGL_O_APPEND) {
            f->cur_pos = f->file_size;
            if (f->file_size > 0 && start >= 2) {
                uint32_t cb = (uint32_t)ctx->cluster_size * ctx->sector_size;
                uint32_t idx = f->file_size / cb;
                f->cur_cluster = fat_chain_get(ctx, start, idx);
                if (f->cur_cluster == 0) f->cur_cluster = start;
            }
        }
        return slot;
    }
    if (!(flags & SGL_O_CREAT)) return FAT_ERR_NOT_FOUND;

    uint32_t nc = 0;
    if (flags & (SGL_O_WRONLY | SGL_O_RDWR)) {
        nc = fat_alloc_cluster(ctx);
        if (nc == 0) return FAT_ERR_NO_SPACE;
        uint32_t base = cluster_to_sector(ctx, nc);
        memset(ctx->sec_buf, 0, ctx->sector_size);
        for (uint8_t s = 0; s < ctx->cluster_size; s++)
            fat_write_sector(ctx, base + s, ctx->sec_buf);
    }
    uint32_t esec = 0; uint16_t eoff = 0;
    ret = fat_create_entry(ctx, parent_dir, fname, FAT_ATTR_ARCHIVE, nc, 0, &esec, &eoff);
    if (ret != FAT_OK) { if (nc >= 2) fat_free_chain(ctx, nc); return ret; }

    fat_file_t *f = &ctx->files[slot];
    memset(f, 0, sizeof(fat_file_t));
    f->used = 1; f->dir_start_cluster = parent_dir;
    f->entry_sector = esec; f->entry_offset = eoff;
    f->start_cluster = nc; f->cur_cluster = nc;
    f->flags = (uint8_t)(flags & 0xFF);
    return slot;
}

static int fatfs_close(void *fs, int fd)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (fd < 0 || fd >= FAT_MAX_OPEN_FILES || !ctx->files[fd].used) return FAT_ERR_INVALID;
    fat_file_t *f = &ctx->files[fd];
    if (f->dirty || (f->flags & (SGL_O_WRONLY | SGL_O_RDWR))) {
        fat_dir_entry_t entry;
        if (fat_cache_read(ctx, f->entry_sector) == FAT_OK) {
            memcpy(&entry, &ctx->sec_buf[f->entry_offset], FAT_DIR_ENTRY_SIZE);
            entry.fst_clus_lo = (uint16_t)(f->start_cluster & 0xFFFF);
            entry.fst_clus_hi = (uint16_t)((f->start_cluster >> 16) & 0xFFFF);
            entry.file_size = f->file_size;
            fat_write_dir_entry(ctx, f->entry_sector, f->entry_offset, &entry);
        }
    }
    f->used = 0;
    fat_cache_flush(ctx);
    return FAT_OK;
}

static int fatfs_read(void *fs, int fd, void *buffer, uint32_t count)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (fd < 0 || fd >= FAT_MAX_OPEN_FILES || !ctx->files[fd].used) return FAT_ERR_INVALID;
    fat_file_t *f = &ctx->files[fd];
    if (!(f->flags & (SGL_O_RDONLY | SGL_O_RDWR))) return FAT_ERR_DENIED;
    if (f->start_cluster < 2) return 0;
    if (f->cur_pos + count > f->file_size) {
        if (f->cur_pos >= f->file_size) return 0;
        count = f->file_size - f->cur_pos;
    }
    if (count == 0) return 0;
    uint32_t cb = (uint32_t)ctx->cluster_size * ctx->sector_size;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t br = 0;
    if (f->cur_cluster < 2) f->cur_cluster = f->start_cluster;
    while (br < count) {
        uint32_t oc = f->cur_pos % cb;
        uint32_t sector = cluster_to_sector(ctx, f->cur_cluster) + oc / ctx->sector_size;
        uint16_t os = (uint16_t)(oc % ctx->sector_size);
        if (fat_cache_read(ctx, sector) != FAT_OK) break;
        uint32_t avail = ctx->sector_size - os;
        uint32_t tc = (count - br < avail) ? count - br : avail;
        memcpy(dst + br, &ctx->sec_buf[os], tc);
        br += tc; f->cur_pos += tc;
        if (f->cur_pos % cb == 0 && br < count) {
            uint32_t next = fat_get_entry(ctx, f->cur_cluster);
            if (next < 2 || is_eoc(ctx, next)) break;
            f->cur_cluster = next;
        }
    }
    return (int)br;
}

static int fatfs_write(void *fs, int fd, const void *buffer, uint32_t count)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (fd < 0 || fd >= FAT_MAX_OPEN_FILES || !ctx->files[fd].used) return FAT_ERR_INVALID;
    fat_file_t *f = &ctx->files[fd];
    if (!(f->flags & (SGL_O_WRONLY | SGL_O_RDWR))) return FAT_ERR_DENIED;
    if (count == 0) return 0;
    uint32_t cb = (uint32_t)ctx->cluster_size * ctx->sector_size;
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t bw = 0;
    if (f->start_cluster < 2) {
        f->start_cluster = fat_alloc_cluster(ctx);
        if (f->start_cluster == 0) return FAT_ERR_NO_SPACE;
        f->cur_cluster = f->start_cluster;
        uint32_t base = cluster_to_sector(ctx, f->cur_cluster);
        memset(ctx->sec_buf, 0, ctx->sector_size);
        for (uint8_t s = 0; s < ctx->cluster_size; s++)
            fat_write_sector(ctx, base + s, ctx->sec_buf);
    }
    if (f->cur_cluster < 2) f->cur_cluster = f->start_cluster;
    while (bw < count) {
        uint32_t oc = f->cur_pos % cb;
        uint32_t sector = cluster_to_sector(ctx, f->cur_cluster) + oc / ctx->sector_size;
        uint16_t os = (uint16_t)(oc % ctx->sector_size);
        uint32_t avail = ctx->sector_size - os;
        uint32_t tw = (count - bw < avail) ? count - bw : avail;
        if (tw < ctx->sector_size) { if (fat_cache_read(ctx, sector) != FAT_OK) break; }
        memcpy(&ctx->sec_buf[os], src + bw, tw);
        fat_cache_dirty(ctx); fat_cache_flush(ctx);
        bw += tw; f->cur_pos += tw;
        if (f->cur_pos > f->file_size) { f->file_size = f->cur_pos; f->dirty = 1; }
        if (f->cur_pos % cb == 0 && bw < count) {
            uint32_t next = fat_get_entry(ctx, f->cur_cluster);
            if (next < 2 || is_eoc(ctx, next)) {
                next = fat_alloc_cluster(ctx);
                if (next == 0) break;
                fat_set_entry(ctx, f->cur_cluster, next);
                uint32_t base = cluster_to_sector(ctx, next);
                memset(ctx->sec_buf, 0, ctx->sector_size);
                for (uint8_t s = 0; s < ctx->cluster_size; s++)
                    fat_write_sector(ctx, base + s, ctx->sec_buf);
            }
            f->cur_cluster = next;
        }
    }
    return (int)bw;
}

static int fatfs_opendir(void *fs, const char *path, int *dd)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    int slot = -1;
    for (int i = 0; i < FAT_MAX_OPEN_DIRS; i++)
        if (!ctx->dirs[i].used) { slot = i; break; }
    if (slot < 0) return FAT_ERR_TOO_MANY_OPEN;

    uint32_t parent_dir; const char *fname;
    int ret = fat_resolve_path(ctx, path, &parent_dir, &fname);
    if (ret != FAT_OK) return ret;

    uint32_t dir_cluster;
    if (fname == NULL) { dir_cluster = parent_dir; }
    else {
        fat_dir_entry_t entry;
        ret = fat_find_in_dir(ctx, parent_dir, fname, &entry, NULL, NULL);
        if (ret != FAT_OK) return ret;
        if (!(entry.attr & FAT_ATTR_DIRECTORY)) return FAT_ERR_NOT_DIR;
        dir_cluster = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (dir_cluster == 0) dir_cluster = root_cluster(ctx);
    }
    fat_dir_t *d = &ctx->dirs[slot];
    d->used = 1; d->start_cluster = dir_cluster;
    d->cur_cluster = dir_cluster; d->entry_index = 0; d->finished = 0;
    *dd = slot;
    return FAT_OK;
}

static int fatfs_readdir(void *fs, int dd, char *name, uint32_t name_size, uint32_t *type)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (dd < 0 || dd >= FAT_MAX_OPEN_DIRS || !ctx->dirs[dd].used) return FAT_ERR_INVALID;
    fat_dir_t *d = &ctx->dirs[dd];
    if (d->finished) return 0;
    uint32_t dc = d->start_cluster;
    uint8_t is_root = (ctx->fat_type != FAT_TYPE_32 && dc == 0);
    while (1) {
        fat_dir_entry_t entry;
        int ret = fat_read_dir_entry(ctx, is_root ? 0 : dc, d->entry_index, &entry, NULL, NULL);
        if (ret != FAT_OK) { d->finished = 1; return 0; }
        d->entry_index++;
        if (entry.name[0] == FAT_ENTRY_FREE) continue;
        if (entry.attr & FAT_ATTR_LONG_NAME) continue;
        if (entry.attr & FAT_ATTR_VOLUME_ID) continue;
        if (name && name_size > 0) name_from_83(entry.name, name, name_size);
        if (type) *type = (entry.attr & FAT_ATTR_DIRECTORY) ? SGL_S_IFDIR : SGL_S_IFREG;
        return 1;
    }
}

static int fatfs_closedir(void *fs, int dd)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (dd < 0 || dd >= FAT_MAX_OPEN_DIRS || !ctx->dirs[dd].used) return FAT_ERR_INVALID;
    ctx->dirs[dd].used = 0;
    return FAT_OK;
}

static int fatfs_sync(void *fs)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    fat_cache_flush(ctx);
    for (int i = 0; i < FAT_MAX_OPEN_FILES; i++) {
        fat_file_t *f = &ctx->files[i];
        if (f->used && (f->dirty || (f->flags & (SGL_O_WRONLY | SGL_O_RDWR)))) {
            fat_dir_entry_t entry;
            if (fat_cache_read(ctx, f->entry_sector) == FAT_OK) {
                memcpy(&entry, &ctx->sec_buf[f->entry_offset], FAT_DIR_ENTRY_SIZE);
                entry.fst_clus_lo = (uint16_t)(f->start_cluster & 0xFFFF);
                entry.fst_clus_hi = (uint16_t)((f->start_cluster >> 16) & 0xFFFF);
                entry.file_size = f->file_size;
                fat_write_dir_entry(ctx, f->entry_sector, f->entry_offset, &entry);
            }
            f->dirty = 0;
        }
    }
    if (ctx->dev->ioctl) ctx->dev->ioctl(ctx->dev, 0, NULL);
    return FAT_OK;
}

static int fatfs_format(void *fs)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (!ctx) return FAT_ERR_INVALID;
    uint32_t total_sec = 0;
    if (ctx->dev->ioctl) ctx->dev->ioctl(ctx->dev, 1, &total_sec);
    if (total_sec == 0) total_sec = ctx->total_sectors;

    uint8_t *buf = ctx->sec_buf;
    memset(buf, 0, ctx->sector_size);
    buf[0] = 0xEB; buf[1] = 0x3C; buf[2] = 0x90;
    memcpy(&buf[3], "SGLFAT  ", 8);
    wr16(&buf[11], ctx->sector_size);
    uint8_t spc = 1;
    if (total_sec > 32768) spc = 8; else if (total_sec > 8192) spc = 4;
    buf[13] = spc;
    wr16(&buf[14], 1); buf[16] = 2;
    uint16_t root_ent = 512; wr16(&buf[17], root_ent);
    if (total_sec <= 0xFFFF) wr16(&buf[19], (uint16_t)total_sec);
    else { wr16(&buf[19], 0); wr32(&buf[32], total_sec); }
    buf[21] = 0xF8;
    uint32_t root_sec = ((uint32_t)root_ent * 32 + ctx->sector_size - 1) / ctx->sector_size;
    uint32_t data_sec = total_sec - 1 - root_sec;
    uint32_t fat_sec = (data_sec / spc * 2 + ctx->sector_size - 1) / ctx->sector_size;
    if (fat_sec == 0) fat_sec = 1;
    wr16(&buf[22], (uint16_t)fat_sec);
    wr16(&buf[24], 63); wr16(&buf[26], 255);
    buf[510] = 0x55; buf[511] = 0xAA;

    fat_cache_invalidate(ctx);
    if (fat_write_sector(ctx, 0, buf) != 0) return FAT_ERR_IO;

    memset(buf, 0, ctx->sector_size);
    wr16(&buf[0], 0xFFF8); wr16(&buf[2], 0xFFFF);
    for (uint8_t f = 0; f < 2; f++) {
        uint32_t base = 1 + (uint32_t)f * fat_sec;
        fat_write_sector(ctx, base, buf);
        memset(buf, 0, ctx->sector_size);
        for (uint32_t s = 1; s < fat_sec; s++) fat_write_sector(ctx, base + s, buf);
    }
    uint32_t root_start = 1 + 2 * fat_sec;
    memset(buf, 0, ctx->sector_size);
    for (uint32_t s = 0; s < root_sec; s++) fat_write_sector(ctx, root_start + s, buf);
    return FAT_OK;
}

static int fatfs_remove(void *fs, const char *path)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    uint32_t parent_dir; const char *fname;
    int ret = fat_resolve_path(ctx, path, &parent_dir, &fname);
    if (ret != FAT_OK) return ret;
    if (fname == NULL) return FAT_ERR_INVALID;

    fat_dir_entry_t entry; uint32_t esec; uint16_t eoff;
    ret = fat_find_in_dir(ctx, parent_dir, fname, &entry, &esec, &eoff);
    if (ret != FAT_OK) return ret;

    if (entry.attr & FAT_ATTR_DIRECTORY) {
        uint32_t dc = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (dc >= 2 && !fat_dir_is_empty(ctx, dc)) return FAT_ERR_NOT_EMPTY;
    }
    uint32_t start = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    if (start >= 2) fat_free_chain(ctx, start);
    entry.name[0] = FAT_ENTRY_FREE;
    fat_write_dir_entry(ctx, esec, eoff, &entry);
    return FAT_OK;
}

static int fatfs_mkdir(void *fs, const char *path)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    uint32_t parent_dir; const char *fname;
    int ret = fat_resolve_path(ctx, path, &parent_dir, &fname);
    if (ret != FAT_OK) return ret;
    if (fname == NULL) return FAT_ERR_INVALID;

    fat_dir_entry_t existing;
    if (fat_find_in_dir(ctx, parent_dir, fname, &existing, NULL, NULL) == FAT_OK)
        return FAT_ERR_EXIST;

    uint32_t nc = fat_alloc_cluster(ctx);
    if (nc == 0) return FAT_ERR_NO_SPACE;
    uint32_t base = cluster_to_sector(ctx, nc);
    memset(ctx->sec_buf, 0, ctx->sector_size);
    for (uint8_t s = 0; s < ctx->cluster_size; s++)
        fat_write_sector(ctx, base + s, ctx->sec_buf);

    fat_dir_entry_t dot;
    memset(&dot, 0, sizeof(dot));
    memset(dot.name, ' ', 11); dot.name[0] = '.';
    dot.attr = FAT_ATTR_DIRECTORY;
    dot.fst_clus_lo = (uint16_t)(nc & 0xFFFF);
    dot.fst_clus_hi = (uint16_t)((nc >> 16) & 0xFFFF);
    fat_write_dir_entry(ctx, base, 0, &dot);

    memset(&dot, 0, sizeof(dot));
    memset(dot.name, ' ', 11); dot.name[0] = '.'; dot.name[1] = '.';
    dot.attr = FAT_ATTR_DIRECTORY;
    uint32_t pc = parent_dir;
    if (ctx->fat_type != FAT_TYPE_32 && pc == root_cluster(ctx)) pc = 0;
    dot.fst_clus_lo = (uint16_t)(pc & 0xFFFF);
    dot.fst_clus_hi = (uint16_t)((pc >> 16) & 0xFFFF);
    fat_write_dir_entry(ctx, base, FAT_DIR_ENTRY_SIZE, &dot);

    uint32_t esec = 0; uint16_t eoff = 0;
    ret = fat_create_entry(ctx, parent_dir, fname, FAT_ATTR_DIRECTORY, nc, 0, &esec, &eoff);
    if (ret != FAT_OK) { fat_free_chain(ctx, nc); return ret; }
    return FAT_OK;
}

static int fatfs_stat(void *fs, const char *path, sgl_stat_t *st)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    if (!st) return FAT_ERR_INVALID;
    uint32_t parent_dir; const char *fname;
    int ret = fat_resolve_path(ctx, path, &parent_dir, &fname);
    if (ret != FAT_OK) return ret;
    if (fname == NULL) {
        memset(st, 0, sizeof(sgl_stat_t));
        st->st_mode = SGL_S_IFDIR | SGL_S_IRWXU | SGL_S_IRWXG | SGL_S_IRWXO;
        return FAT_OK;
    }
    fat_dir_entry_t entry;
    ret = fat_find_in_dir(ctx, parent_dir, fname, &entry, NULL, NULL);
    if (ret != FAT_OK) return ret;
    st->st_size = entry.file_size;
    st->st_mode = (entry.attr & FAT_ATTR_DIRECTORY) ? SGL_S_IFDIR : SGL_S_IFREG;
    st->st_mode |= SGL_S_IRWXU | SGL_S_IRWXG | SGL_S_IRWXO;
    st->st_mtime = 0;
    return FAT_OK;
}

static int fatfs_rename(void *fs, const char *old_path, const char *new_path)
{
    fat_ctx_t *ctx = (fat_ctx_t *)fs;
    uint32_t old_parent; const char *old_name;
    int ret = fat_resolve_path(ctx, old_path, &old_parent, &old_name);
    if (ret != FAT_OK) return ret;
    if (old_name == NULL) return FAT_ERR_INVALID;

    fat_dir_entry_t entry; uint32_t old_sec; uint16_t old_off;
    ret = fat_find_in_dir(ctx, old_parent, old_name, &entry, &old_sec, &old_off);
    if (ret != FAT_OK) return ret;

    uint32_t new_parent; const char *new_name;
    ret = fat_resolve_path(ctx, new_path, &new_parent, &new_name);
    if (ret != FAT_OK) return ret;
    if (new_name == NULL) return FAT_ERR_INVALID;

    fat_dir_entry_t existing; uint32_t ex_sec; uint16_t ex_off;
    if (fat_find_in_dir(ctx, new_parent, new_name, &existing, &ex_sec, &ex_off) == FAT_OK) {
        uint32_t ec = ((uint32_t)existing.fst_clus_hi << 16) | existing.fst_clus_lo;
        if (ec >= 2) fat_free_chain(ctx, ec);
        existing.name[0] = FAT_ENTRY_FREE;
        fat_write_dir_entry(ctx, ex_sec, ex_off, &existing);
    }
    uint32_t start = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    uint32_t ns; uint16_t no;
    ret = fat_create_entry(ctx, new_parent, new_name, entry.attr, start, entry.file_size, &ns, &no);
    if (ret != FAT_OK) return ret;
    entry.name[0] = FAT_ENTRY_FREE;
    fat_write_dir_entry(ctx, old_sec, old_off, &entry);
    return FAT_OK;
}

/* ===================== FS Registration ===================== */

static sgl_fs_ops_t g_fatfs_ops = {
    .mount    = fatfs_mount,
    .unmount  = fatfs_unmount,
    .open     = fatfs_open,
    .close    = fatfs_close,
    .read     = fatfs_read,
    .write    = fatfs_write,
    .opendir  = fatfs_opendir,
    .readdir  = fatfs_readdir,
    .closedir = fatfs_closedir,
    .sync     = fatfs_sync,
    .format   = fatfs_format,
    .remove   = fatfs_remove,
    .mkdir    = fatfs_mkdir,
    .stat     = fatfs_stat,
    .rename   = fatfs_rename,
};

static sgl_fs_type_t g_fatfs_type = {
    .name = "fatfs",
    .ops  = &g_fatfs_ops,
};

int sgl_fatfs_register(void)
{
    memset(g_vol_dev_map, 0, sizeof(g_vol_dev_map));
    return sgl_fs_register(&g_fatfs_type);
}
