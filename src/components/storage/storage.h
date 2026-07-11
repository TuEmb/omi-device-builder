#ifndef OMI_STORAGE_H
#define OMI_STORAGE_H

#include <stdint.h>

/*
 * Storage component — a thin wrapper that mounts the board's mass-storage
 * backend (SD card or external NAND flash) as a Zephyr filesystem at a fixed
 * mount point. Consumers use the standard Zephyr FS API (<zephyr/fs/fs.h>) on
 * paths under STORAGE_MOUNT_POINT; the backend (FATFS on SD, littlefs on NAND)
 * is chosen at build time via CONFIG_OMI_STORAGE_BACKEND_*.
 */

#define STORAGE_MOUNT_POINT "/omi"

/* Mount the storage filesystem. Returns 0 on success, negative errno on error. */
int storage_init(void);

/* Total / available bytes on the mounted filesystem (best-effort). */
int storage_space(uint64_t *total_bytes, uint64_t *free_bytes);

#endif /* OMI_STORAGE_H */
