/*
 * Storage backend: SD card (FATFS over disk_access).
 *
 * Board contract (devicetree): a `zephyr,sdmmc-disk` node (SD over SPI or the
 * SDHC controller) whose `disk-name` matches CONFIG_OMI_STORAGE_SD_DISK_NAME
 * (default "SD"). Selected with CONFIG_OMI_STORAGE_BACKEND_SD.
 */

#include "storage.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <ff.h>

LOG_MODULE_REGISTER(storage, CONFIG_LOG_DEFAULT_LEVEL);

#define SD_DISK_NAME CONFIG_OMI_STORAGE_SD_DISK_NAME

static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = STORAGE_MOUNT_POINT,
};

int storage_init(void)
{
    if (disk_access_init(SD_DISK_NAME) != 0) {
        LOG_ERR("SD: disk_access_init(%s) failed (card present?)", SD_DISK_NAME);
        return -EIO;
    }

    /* FATFS wants the disk name as the drive in the mount point's fs data. */
    static const char disk_pdrv[] = SD_DISK_NAME ":";
    mp.storage_dev = (void *) disk_pdrv;

    int rc = fs_mount(&mp);
    if (rc != 0) {
        LOG_ERR("SD: fs_mount failed: %d", rc);
        return rc;
    }
    LOG_INF("SD card mounted at %s", STORAGE_MOUNT_POINT);
    return 0;
}

int storage_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    struct fs_statvfs sb;
    int rc = fs_statvfs(STORAGE_MOUNT_POINT, &sb);
    if (rc != 0) {
        return rc;
    }
    if (total_bytes) {
        *total_bytes = (uint64_t) sb.f_frsize * sb.f_blocks;
    }
    if (free_bytes) {
        *free_bytes = (uint64_t) sb.f_frsize * sb.f_bfree;
    }
    return 0;
}
