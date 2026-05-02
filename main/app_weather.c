#include "app_weather.h"
#include "sys_wifi.h"
#include "sys_config.h"
#include "secrets.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "weather";

#define LOCATION_ID   "101010300"  /* Beijing Chaoyang */

static lv_obj_t *root = NULL;
static lv_obj_t *status_lbl = NULL;
static bool data_ready = false;
static char weather_text[64];
static char weather_temp[32];
static char weather_hum[32];
static char weather_wind[32];
static char weather_city[32] = "Beijing";
static char forecast[3][64];  /* 3-day forecast lines */

static void parse_current(const char *json)
{
    cJSON *root_j = cJSON_Parse(json);
    if (!root_j) return;
    cJSON *now = cJSON_GetObjectItem(root_j, "now");
    if (now) {
        cJSON *t = cJSON_GetObjectItem(now, "text");
        cJSON *tp = cJSON_GetObjectItem(now, "temp");
        cJSON *hm = cJSON_GetObjectItem(now, "humidity");
        cJSON *wd = cJSON_GetObjectItem(now, "windDir");
        cJSON *ws = cJSON_GetObjectItem(now, "windScale");
        cJSON *fl = cJSON_GetObjectItem(now, "feelsLike");

        snprintf(weather_text, sizeof(weather_text), "%s", t ? t->valuestring : "--");
        snprintf(weather_temp, sizeof(weather_temp), "%sC",
                 tp ? tp->valuestring : "--");
        snprintf(weather_hum, sizeof(weather_hum), "Hum: %s%%",
                 hm ? hm->valuestring : "--");
        snprintf(weather_wind, sizeof(weather_wind), "%s %s",
                 wd ? wd->valuestring : "", ws ? ws->valuestring : "");
    }
    cJSON_Delete(root_j);
}

static void parse_forecast(const char *json)
{
    cJSON *root_j = cJSON_Parse(json);
    if (!root_j) return;
    cJSON *daily = cJSON_GetObjectItem(root_j, "daily");
    if (daily) {
        int i = 0;
        cJSON *day;
        cJSON_ArrayForEach(day, daily) {
            if (i >= 3) break;
            cJSON *d = cJSON_GetObjectItem(day, "fxDate");
            cJSON *hi = cJSON_GetObjectItem(day, "tempMax");
            cJSON *lo = cJSON_GetObjectItem(day, "tempMin");
            cJSON *tx = cJSON_GetObjectItem(day, "textDay");

            const char *date = d ? d->valuestring : "?";
            /* Show MM-DD only */
            const char *ds = strchr(date, '-');
            ds = ds ? strchr(ds + 1, '-') : date;
            if (ds) ds++; else ds = date;

            snprintf(forecast[i], sizeof(forecast[i]), "%s %s %s/%sC",
                     ds,
                     tx ? tx->valuestring : "--",
                     hi ? hi->valuestring : "--",
                     lo ? lo->valuestring : "--");
            i++;
        }
    }
    cJSON_Delete(root_j);
}

static esp_err_t http_get(const char *url, char *buf, size_t buf_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            int len = esp_http_client_read(client, buf, buf_len - 1);
            if (len > 0) buf[len] = '\0';
            else err = ESP_FAIL;
        } else {
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

static void fetch_task(void *arg)
{
    char url[512];
    char key[128];
    sys_config_get_weather_key(key, sizeof(key));
    if (key[0] == '\0') strncpy(key, QWEATHER_API_KEY, sizeof(key));

    /* Current weather */
    snprintf(url, sizeof(url),
             "https://devapi.qweather.com/v7/weather/now?location=%s&key=%s",
             LOCATION_ID, key);

    char buf[4096];
    if (http_get(url, buf, sizeof(buf)) == ESP_OK) {
        parse_current(buf);
    } else {
        ESP_LOGW(TAG, "Failed to fetch current weather");
    }

    /* 3-day forecast */
    snprintf(url, sizeof(url),
             "https://devapi.qweather.com/v7/weather/3d?location=%s&key=%s",
             LOCATION_ID, key);

    if (http_get(url, buf, sizeof(buf)) == ESP_OK) {
        parse_forecast(buf);
    }

    data_ready = true;
    vTaskDelete(NULL);
}

static void update_ui(lv_timer_t *t)
{
    if (!data_ready) return;
    lv_timer_del(t);

    /* Clear loading */
    lv_obj_clean(root);

    /* City */
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, weather_city);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xcccccc), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);

    /* Temperature (large) */
    l = lv_label_create(root);
    lv_label_set_text(l, weather_temp);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_shadow_color(l, lv_color_hex(0x4488cc), 0);
    lv_obj_set_style_shadow_width(l, 16, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 40);

    /* Condition text */
    l = lv_label_create(root);
    lv_label_set_text(l, weather_text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 100);

    /* Details row */
    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "%s    %s", weather_hum, weather_wind);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888899), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 130);

    /* Divider */
    lv_obj_t *div = lv_obj_create(root);
    lv_obj_set_size(div, 300, 1);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div, 0, 0);

    /* Forecast title */
    l = lv_label_create(root);
    lv_label_set_text(l, "Forecast");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x666688), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 170);

    /* 3-day forecast */
    for (int i = 0; i < 3; i++) {
        l = lv_label_create(root);
        lv_label_set_text(l, forecast[i][0] ? forecast[i] : "N/A");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x9999aa), 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 195 + i * 25);
    }
}

static void create(lv_obj_t *parent)
{
    data_ready = false;
    memset(forecast, 0, sizeof(forecast));
    memset(weather_text, 0, sizeof(weather_text));

    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    /* Loading */
    status_lbl = lv_label_create(root);
    lv_label_set_text(status_lbl, "Loading...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_center(status_lbl);

    /* Start HTTP fetch in background */
    xTaskCreate(fetch_task, "wx_fetch", 8192, NULL, 3, NULL);

    /* Poll for data every 200ms */
    lv_timer_create(update_ui, 200, NULL);
}

static void destroy(void)
{
    if (root) { lv_obj_del(root); root = NULL; }
}

static void resume(void) {}

const app_entry_t app_weather = {
    .name = "Weather",
    .create = create, .destroy = destroy, .resume = resume,
};
