#include "sys_sdcard.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include <sys/stat.h>

static const char *TAG = "sys_sd";

#define SD_MOUNT_POINT  "/sdcard"

static bool mounted = false;

void sys_sdcard_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

    /* ESP32-S3 with octal PSRAM: GPIO33-37 used by PSRAM.
       SDMMC default pins conflict. Use 1-bit mode with safer pins. */
    slot.width = 1;  /* 1-bit mode, only needs CLK, CMD, D0 */

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);

    if (ret == ESP_OK) {
        mounted = true;
        ESP_LOGI(TAG, "SD card mounted, %lluMB",
                 (uint64_t)(card->csd.capacity) * 512 / 1024 / 1024);
    } else {
        ESP_LOGW(TAG, "No SD card (err=0x%x)", ret);
    }
}

bool sys_sdcard_mounted(void)          { return mounted; }
const char *sys_sdcard_mount_point(void) { return SD_MOUNT_POINT; }

bool sys_sdcard_file_exists(const char *path)
{
    if (!mounted) return false;
    struct stat st;
    char full[128];
    snprintf(full, sizeof(full), "%s/%s", SD_MOUNT_POINT, path);
    return stat(full, &st) == 0;
}

int sys_sdcard_file_size(const char *path)
{
    if (!mounted) return 0;
    struct stat st;
    char full[128];
    snprintf(full, sizeof(full), "%s/%s", SD_MOUNT_POINT, path);
    return stat(full, &st) == 0 ? (int)st.st_size : 0;
}
