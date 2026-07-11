/*
 * Storage backend: external NAND/NOR flash (littlefs).
 *
 * Board contract (devicetree): an external flash device (e.g. `spi-nand`) with
 * a fixed-partition labelled `omi_storage` (nodelabel `omi_storage`). littlefs
 * is formatted/mounted on that partition. Selected with
 * CONFIG_OMI_STORAGE_BACKEND_NAND.
 */

#include "storage.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(storage, CONFIG_LOG_DEFAULT_LEVEL);

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage_lfs);

static struct fs_mount_t mp = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_lfs,
    .storage_dev = (void *) FIXED_PARTITION_ID(omi_storage),
    .mnt_point = STORAGE_MOUNT_POINT,
};

int storage_init(void)
{
    int rc = fs_mount(&mp);
    if (rc != 0) {
        LOG_ERR("NAND: littlefs mount failed: %d", rc);
        return rc;
    }
    LOG_INF("NAND flash (littlefs) mounted at %s", STORAGE_MOUNT_POINT);
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
