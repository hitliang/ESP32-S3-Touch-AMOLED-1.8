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
#define LOCATION_ID   "101010300"

static lv_obj_t *root = NULL;
static lv_timer_t *poll_timer = NULL;
static int state = 0;  /* 0=fetching, 1=done, -1=error */
static char wx_txt[64], wx_tmp[32], wx_hum[32], wx_wind[32];
static char fc[3][64];

static esp_err_t http_get(const char *path, char *buf, int max)
{
    char url[512];
    snprintf(url, sizeof(url), "https://%s%s&key=%s",
             QWEATHER_API_HOST, path, QWEATHER_API_KEY);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(cli);
    int status = err == ESP_OK ? esp_http_client_get_status_code(cli) : -1;
    int content_len = esp_http_client_get_content_length(cli);
    printf("WX: err=%d status=%d len=%d\n", err, status, content_len);
    if (err == ESP_OK && status == 200) {
        int len = esp_http_client_read(cli, buf, max - 1);
        printf("WX: read=%d\n", len);
        if (len > 0) { buf[len] = 0; } else { err = ESP_FAIL; }
    } else {
        err = ESP_FAIL;
    }
    esp_http_client_cleanup(cli);
    return err;
}

static void do_fetch(void)
{
    if (!sys_wifi_is_connected()) { state = -1; return; }

    char buf[4096];

    esp_err_t ret = http_get("/v7/weather/now?location=" LOCATION_ID, buf, sizeof(buf));
    printf("WX: now ret=%d\n", ret);
    if (ret == ESP_OK) {
        printf("WX: %.200s\n", buf);
        cJSON *j = cJSON_Parse(buf);
        if (j) {
            cJSON *n = cJSON_GetObjectItem(j, "now");
            if (n) {
                cJSON *t = cJSON_GetObjectItem(n, "text");
                cJSON *tp = cJSON_GetObjectItem(n, "temp");
                cJSON *hm = cJSON_GetObjectItem(n, "humidity");
                cJSON *wd = cJSON_GetObjectItem(n, "windDir");
                cJSON *ws = cJSON_GetObjectItem(n, "windScale");
                snprintf(wx_txt, sizeof(wx_txt), "%s", t ? t->valuestring : "--");
                snprintf(wx_tmp, sizeof(wx_tmp), "%sC", tp ? tp->valuestring : "--");
                snprintf(wx_hum, sizeof(wx_hum), "Hum: %s%%", hm ? hm->valuestring : "--");
                snprintf(wx_wind, sizeof(wx_wind), "%s %s",
                         wd ? wd->valuestring : "", ws ? ws->valuestring : "");
            }
            cJSON_Delete(j);
        }
    }

    ret = http_get("/v7/weather/3d?location=" LOCATION_ID, buf, sizeof(buf));
    printf("WX: 3d ret=%d\n", ret);
    if (ret == ESP_OK) {
        cJSON *j = cJSON_Parse(buf);
        if (j) {
            cJSON *daily = cJSON_GetObjectItem(j, "daily");
            if (daily) {
                int i = 0;
                cJSON *day;
                cJSON_ArrayForEach(day, daily) {
                    if (i >= 3) break;
                    cJSON *d = cJSON_GetObjectItem(day, "fxDate");
                    cJSON *hi = cJSON_GetObjectItem(day, "tempMax");
                    cJSON *lo = cJSON_GetObjectItem(day, "tempMin");
                    cJSON *tx = cJSON_GetObjectItem(day, "textDay");
                    const char *ds = d ? d->valuestring : "?";
                    const char *p = strchr(ds, '-');
                    p = p ? strchr(p + 1, '-') : ds;
                    if (p) p++; else p = ds;
                    snprintf(fc[i], sizeof(fc[i]), "%s %s %s/%sC",
                             p, tx ? tx->valuestring : "--",
                             hi ? hi->valuestring : "--",
                             lo ? lo->valuestring : "--");
                    i++;
                }
            }
            cJSON_Delete(j);
        }
    }
    state = 1;
}

static void update_cb(lv_timer_t *t)
{
    if (state == 0) return;
    lv_timer_del(t);
    lv_obj_clean(root);

    if (state < 0 || wx_tmp[0] == 0) {
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
    lv_label_set_text(l, wx_tmp);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_shadow_color(l, lv_color_hex(0x4488cc), 0);
    lv_obj_set_style_shadow_width(l, 16, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 40);

    l = lv_label_create(root);
    lv_label_set_text(l, wx_txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 100);

    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "%s    %s", wx_hum, wx_wind);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888899), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *d = lv_obj_create(root);
    lv_obj_set_size(d, 300, 1);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(d, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(d, 0, 0);

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
    printf("WX: fetch start, wifi=%d\n", sys_wifi_is_connected());
    int w = 0;
    while (!sys_wifi_is_connected() && w < 50) {
        vTaskDelay(pdMS_TO_TICKS(200)); w++;
    }
    printf("WX: wifi ready=%d after %d\n", sys_wifi_is_connected(), w);
    do_fetch();
    printf("WX: fetch done, state=%d\n", state);
    vTaskDelete(NULL);
}

static void create(lv_obj_t *parent)
{
    state = 0;
    memset(fc, 0, sizeof(fc));
    memset(wx_txt, 0, sizeof(wx_txt));
    memset(wx_tmp, 0, sizeof(wx_tmp));

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
    poll_timer = lv_timer_create(update_cb, 300, NULL);
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
