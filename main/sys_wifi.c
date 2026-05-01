#include "sys_wifi.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_log.h"

static const char *TAG = "sys_wifi";

#define WIFI_SSID     "BABY&L"
#define WIFI_PASS     "LiangBaby123."

static wifi_status_t wifi_status = WIFI_DISCONNECTED;
static int wifi_rssi = 0;
static bool time_synced = false;
static esp_netif_t *sta_netif = NULL;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_status = WIFI_CONNECTED;
    ESP_LOGI(TAG, "Got IP, starting SNTP");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
}

static void on_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_status = WIFI_DISCONNECTED;
    ESP_LOGW(TAG, "WiFi disconnected, retrying...");
    esp_wifi_connect();
}

static void on_time_sync(struct timeval *tv)
{
    time_synced = true;
    ESP_LOGI(TAG, "NTP synced");

    /* Set timezone CST-8 */
    setenv("TZ", "CST-8", 1);
    tzset();
}

void sys_wifi_init(void)
{
    ESP_LOGI(TAG, "Init WiFi STA");

    esp_netif_init();
    esp_event_loop_create_default();
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_disconnected, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Register NTP sync callback */
    sntp_set_time_sync_notification_cb(on_time_sync);

    wifi_status = WIFI_CONNECTING;
    esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to %s...", WIFI_SSID);
}

wifi_status_t sys_wifi_get_status(void)    { return wifi_status; }
bool          sys_wifi_is_connected(void)  { return wifi_status == WIFI_CONNECTED; }
bool          sys_time_is_synced(void)     { return time_synced; }

int sys_wifi_get_rssi(void)
{
    esp_wifi_sta_get_ap_info(NULL);
    return wifi_rssi;
}

struct tm sys_time_get_tm(void)
{
    time_t now;
    struct tm tm_now = {0};
    time(&now);
    localtime_r(&now, &tm_now);
    return tm_now;
}

time_t sys_time_get_unix(void)
{
    return time(NULL);
}
