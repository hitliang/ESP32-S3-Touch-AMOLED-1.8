#include "sys_sdcard.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

static const char *TAG = "sys_sd";

#define SD_MOUNT_POINT  "/sdcard"

static bool mounted = false;

void sys_sdcard_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    /* Check for SD card pins: CLK, CMD, D0 standard for ESP32-S3 SDMMC */
    slot.clk = GPIO_NUM_36;
    slot.cmd = GPIO_NUM_35;
    slot.d0  = GPIO_NUM_37;

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);

    if (ret == ESP_OK) {
        mounted = true;
        ESP_LOGI(TAG, "Mounted, size=%lluMB", (uint64_t)(card->csd.capacity) * 512 / 1024 / 1024);
    } else {
        ESP_LOGW(TAG, "No SD card (err=0x%x)", ret);
    }
}

bool        sys_sdcard_mounted(void)     { return mounted; }
const char *sys_sdcard_mount_point(void) { return SD_MOUNT_POINT; }
