/* sgl/examples/filebrowser.c
 *
 * File browser example.
 *
 * Two simulated block devices are mounted with RAMFS ("/flash" and "/sd"),
 * seeded with sample files, then a single sgl_filebrowser widget opens at the
 * virtual "/" root, which lists both devices by name; clicking one enters it.
 */
#include <sgl.h>
#include <string.h>

/* Simulated block devices. RAMFS ignores the IO callbacks; it only needs a
 * distinct, stable pointer to identify each "disk". The info->name is what the
 * file browser shows in its device list. */
static const sgl_block_dev_info_t g_flash_info = { .name = "flash" };
static const sgl_block_dev_info_t g_sd_info    = { .name = "sd" };
static sgl_block_dev_t g_flash_dev = { .info = &g_flash_info };
static sgl_block_dev_t g_sd_dev    = { .info = &g_sd_info };

/* Give each RAMFS a capacity so the browser's device list can show the
 * total / available space next to each device name. */
static sgl_ramfs_config_t g_flash_cfg = { .max_size =  2 * 1024 * 1024 }; /* 2 MB  */
static sgl_ramfs_config_t g_sd_cfg     = { .max_size = 16 * 1024 * 1024 }; /* 16 MB */

static void seed_file(const char *path, const char *content)
{
    int fd = sgl_fs_open(path, SGL_O_RDWR | SGL_O_CREAT | SGL_O_TRUNC);
    if (fd < 0) {
        SGL_LOG_ERROR("filebrowser example: seed %s failed", path);
        return;
    }
    sgl_fs_write(fd, content, (uint32_t)strlen(content));
    sgl_fs_close(fd);
}

static void seed_demo_content(void)
{
    /* /flash: fake NOR flash */
    sgl_fs_mkdir("/flash/docs");
    seed_file("/flash/readme.txt", "SGL filebrowser demo on /flash");
    seed_file("/flash/boot.ini",  "log=0 level=info");
    seed_file("/flash/docs/note.md", "hello from the docs directory");

    /* /sd: fake SD card */
    sgl_fs_mkdir("/sd/music");
    seed_file("/sd/track01.mp3", "fake mp3 data 1");
    seed_file("/sd/track02.mp3", "fake mp3 data 2");
    seed_file("/sd/music/loop.wav", "fake wav data");
}

void sgl_filebrowser_examples(sgl_obj_t *parent)
{
    /* 1. mount one filesystem per block device */
    sgl_ramfs_register();   /* ignore "already registered" */
    if (sgl_fs_mount("/flash", "ramfs", &g_flash_dev, &g_flash_cfg) != 0) {
        SGL_LOG_ERROR("filebrowser example: mount /flash failed");
        return;
    }

    if (sgl_fs_mount("/sd", "ramfs", &g_sd_dev, &g_sd_cfg) != 0) {
        SGL_LOG_ERROR("filebrowser example: mount /sd failed");
        return;
    }
    seed_demo_content();

    /* 2. one browser opens at the "/" device list; the user clicks a device
     *    ("flash" / "sd") to enter its file system. Placed on the right half
     *    so the avi demo can keep the left. */
    sgl_obj_t *fb = sgl_filebrowser_create(parent);
    sgl_obj_set_pos(fb, 380, 20);
    sgl_obj_set_size(fb, 405, 260);
    sgl_filebrowser_set_text_font(fb, &consolas14);
}
