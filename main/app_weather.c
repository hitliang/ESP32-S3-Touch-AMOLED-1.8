#include "app_weather.h"
#include "sys_wifi.h"
#include "secrets.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "weather";
#define CITY_CODE  "110105"  /* Beijing Chaoyang */

static lv_obj_t *root = NULL;
static int state = 0;  /* 0=fetching, 1=ok, -1=error */
static char wx_today[64], wx_temp[32], wx_wind[32];
static char fc[3][64];

typedef struct {
    char *buf;
    int   len;
    int   max;
    int   status;
} http_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->len + evt->data_len < ctx->max) {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len += evt->data_len;
            ctx->buf[ctx->len] = 0;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ctx->status = esp_http_client_get_status_code(evt->client);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t http_get(const char *url, char *buf, int max)
{
    http_ctx_t ctx = { .buf = buf, .len = 0, .max = max, .status = 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &ctx,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);

    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);

    return (err == ESP_OK && ctx.status == 200 && ctx.len > 0) ? ESP_OK : ESP_FAIL;
}

static const char *cn2en(const char *cn)
{
    if (!cn) return "?";
    if (strstr(cn, "晴")) return "Clear";
    if (strstr(cn, "多云")) return "Cloudy";
    if (strstr(cn, "阴")) return "Overcast";
    if (strstr(cn, "雨")) return "Rain";
    if (strstr(cn, "雪")) return "Snow";
    if (strstr(cn, "雾") || strstr(cn, "霾")) return "Haze";
    if (strstr(cn, "沙")) return "Dust";
    if (strstr(cn, "风")) return "Windy";
    if (strstr(cn, "雷")) return "Storm";
    return cn;
}

static const char *wind2en(const char *cn)
{
    if (!cn) return "?";
    if (strstr(cn, "北")) return "N";
    if (strstr(cn, "南")) return "S";
    if (strstr(cn, "东")) return "E";
    if (strstr(cn, "西")) return "W";
    if (strstr(cn, "东北")) return "NE";
    if (strstr(cn, "西北")) return "NW";
    if (strstr(cn, "东南")) return "SE";
    if (strstr(cn, "西南")) return "SW";
    return cn;
}

static void do_fetch(void)
{
    if (!sys_wifi_is_connected()) { state = -1; return; }

    char url[384];
    char buf[4096];

    snprintf(url, sizeof(url),
             "https://restapi.amap.com/v3/weather/weatherInfo?"
             "city=%s&key=%s&extensions=all",
             CITY_CODE, AMAP_API_KEY);

    if (http_get(url, buf, sizeof(buf)) != ESP_OK) {
        printf("WX: fetch failed\n");
        state = -1;
        return;
    }
    printf("WX: ok\n");

    cJSON *j = cJSON_Parse(buf);
    if (!j) { state = -1; return; }

    cJSON *forecasts = cJSON_GetObjectItem(j, "forecasts");
    if (forecasts) {
        cJSON *fc_obj = cJSON_GetArrayItem(forecasts, 0);
        if (fc_obj) {
            cJSON *casts = cJSON_GetObjectItem(fc_obj, "casts");
            if (casts && cJSON_GetArraySize(casts) >= 4) {
                cJSON *today = cJSON_GetArrayItem(casts, 0);
                cJSON *dw = cJSON_GetObjectItem(today, "dayweather");
                cJSON *dt = cJSON_GetObjectItem(today, "daytemp");
                cJSON *dwd = cJSON_GetObjectItem(today, "daywind");
                cJSON *dwp = cJSON_GetObjectItem(today, "daypower");
                snprintf(wx_today, sizeof(wx_today), "%s",
                         cn2en(dw ? dw->valuestring : NULL));
                snprintf(wx_temp, sizeof(wx_temp), "%sC",
                         dt ? dt->valuestring : "--");
                snprintf(wx_wind, sizeof(wx_wind), "%s %s",
                         wind2en(dwd ? dwd->valuestring : NULL),
                         dwp ? dwp->valuestring : "");

                for (int i = 0; i < 3; i++) {
                    cJSON *day = cJSON_GetArrayItem(casts, i + 1);
                    cJSON *d = cJSON_GetObjectItem(day, "date");
                    cJSON *w = cJSON_GetObjectItem(day, "dayweather");
                    cJSON *hi = cJSON_GetObjectItem(day, "daytemp");
                    cJSON *lo = cJSON_GetObjectItem(day, "nighttemp");
                    const char *ds = d ? d->valuestring : "?";
                    const char *p = ds + 5; /* skip "2026-" to get MM-DD */
                    snprintf(fc[i], sizeof(fc[i]), "%s %s %s/%sC",
                             p,
                             cn2en(w ? w->valuestring : NULL),
                             hi ? hi->valuestring : "--",
                             lo ? lo->valuestring : "--");
                }
            }
        }
    }
    cJSON_Delete(j);
    state = 1;
}

static void update_cb(lv_timer_t *t)
{
    if (state == 0) return;
    lv_timer_del(t);
    lv_obj_clean(root);

    if (state < 0) {
        lv_obj_t *l = lv_label_create(root);
        lv_label_set_text(l, "No WiFi\nor API error");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x888888), 0);
        lv_obj_center(l);
        return;
    }

    lv_obj_t *l;

    l = lv_label_create(root);
    lv_label_set_text(l, "Beijing Chaoyang");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xcccccc), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);

    l = lv_label_create(root);
    lv_label_set_text(l, wx_temp);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_shadow_color(l, lv_color_hex(0x4488cc), 0);
    lv_obj_set_style_shadow_width(l, 16, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 40);

    l = lv_label_create(root);
    lv_label_set_text(l, wx_today);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 100);

    l = lv_label_create(root);
    lv_label_set_text(l, wx_wind);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888899), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *div = lv_obj_create(root);
    lv_obj_set_size(div, 300, 1);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div, 0, 0);

    l = lv_label_create(root);
    lv_label_set_text(l, "Forecast");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x666688), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 170);

    for (int i = 0; i < 3; i++) {
        l = lv_label_create(root);
        lv_label_set_text(l, fc[i][0] ? fc[i] : "--");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x9999aa), 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 195 + i * 25);
    }
}

static void fetch_task(void *arg)
{
    int w = 0;
    while (!sys_wifi_is_connected() && w < 50) {
        vTaskDelay(pdMS_TO_TICKS(200)); w++;
    }
    do_fetch();
    vTaskDelete(NULL);
}

static void create(lv_obj_t *parent)
{
    state = 0;
    memset(fc, 0, sizeof(fc));
    memset(wx_today, 0, sizeof(wx_today));
    memset(wx_temp, 0, sizeof(wx_temp));

    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "Loading...");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888888), 0);
    lv_obj_center(l);

    xTaskCreate(fetch_task, "wx", 16384, NULL, 3, NULL);
    lv_timer_create(update_cb, 300, NULL);
}

static void destroy(void) { if (root) { lv_obj_del(root); root = NULL; } }
static void resume(void) {}

const app_entry_t app_weather = {
    .name = "Weather",
    .create = create, .destroy = destroy, .resume = resume,
};
