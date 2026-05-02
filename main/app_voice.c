#include "app_voice.h"
#include "sys_wifi.h"
#include "sys_sdcard.h"
#include "secrets.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HISTORY_FILE    "voice_history.txt"
#define MAX_HISTORY     200
#define BODY_MAX        32768

/* ---- System Prompt ---- */
static const char *SYSTEM_PROMPT =
    "你是一个给6岁小男孩用的语音助手。"
    "男孩中文名叫瑞迪，英文名Reddy，上小学一年级。"
    "规则：用简单中文，句子短，适合6岁小孩。"
    "语气温暖耐心，不要讲可怕内容。回答简短，不超过三句话。";

/* ---- JSON escape ---- */
static char *json_esc(const char *s, char *buf, int max)
{
    char *d = buf;
    while (*s && d < buf + max - 6) {
        switch (*s) {
        case '"':  *d++ = '\\'; *d++ = '"';  break;
        case '\\': *d++ = '\\'; *d++ = '\\'; break;
        case '\n': *d++ = '\\'; *d++ = 'n';  break;
        case '\r': *d++ = '\\'; *d++ = 'r';  break;
        case '\t': *d++ = '\\'; *d++ = 't';  break;
        default:   *d++ = *s; break;
        }
        s++;
    }
    *d = 0;
    return buf;
}

/* ---- HTTP ---- */
typedef struct { uint8_t *buf; int len, max, status; } hctx_t;
static esp_err_t hevt(esp_http_client_event_t *e)
{
    hctx_t *c = (hctx_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && c->len + e->data_len < c->max) {
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len; c->buf[c->len] = 0;
    }
    if (e->event_id == HTTP_EVENT_ON_FINISH)
        c->status = esp_http_client_get_status_code(e->client);
    return ESP_OK;
}

static esp_err_t http_post(const char *url, const char *body,
                            const char *h1, const char *v1,
                            const char *h2, const char *v2,
                            uint8_t *buf, int max, int *olen, int *osta)
{
    hctx_t ctx = { .buf = buf, .len = 0, .max = max, .status = 0 };
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 25000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = hevt, .user_data = &ctx,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    if (h1) esp_http_client_set_header(cli, h1, v1);
    if (h2) esp_http_client_set_header(cli, h2, v2);
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);
    *olen = ctx.len; *osta = ctx.status;
    return err;
}

/* ---- History ---- */
static char *history_user[MAX_HISTORY];
static char *history_asst[MAX_HISTORY];
static int   history_count = 0;

static void history_save_to_sd(void)
{
    if (!sys_sdcard_mounted()) return;
    char p[128]; snprintf(p, sizeof(p), "%s/" HISTORY_FILE, sys_sdcard_mount_point());
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < history_count; i++) {
        if (history_user[i]) fprintf(f, "U:%s\n", history_user[i]);
        if (history_asst[i]) fprintf(f, "A:%s\n", history_asst[i]);
    }
    fclose(f);
}

static void history_load_from_sd(void)
{
    if (!sys_sdcard_mounted()) return;
    char p[128]; snprintf(p, sizeof(p), "%s/" HISTORY_FILE, sys_sdcard_mount_point());
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f) && history_count < MAX_HISTORY) {
        int len = strlen(line); if (len>0 && line[len-1]=='\n') line[len-1]=0;
        if (line[0]=='U' && line[1]==':') history_user[history_count] = strdup(line+2);
        else if (line[0]=='A' && line[1]==':') {
            history_asst[history_count] = strdup(line+2); history_count++;
        }
    }
    fclose(f);
}

static void history_add(const char *u, const char *a)
{
    if (history_count >= MAX_HISTORY - 5) {
        char s[2048]="[Earlier: "; int n=10, keep=history_count-n;
        for (int i=0; i<n && strlen(s)<sizeof(s)-100; i++)
            snprintf(s+strlen(s), sizeof(s)-strlen(s), "Q:%s A:%s ", history_user[i]?:"?", history_asst[i]?:"?");
        strncat(s,"]",sizeof(s)-strlen(s)-1);
        for (int i=0;i<n;i++){free(history_user[i]);free(history_asst[i]);}
        for (int i=0;i<keep;i++){history_user[i]=history_user[i+n];history_asst[i]=history_asst[i+n];}
        for (int i=keep;i<MAX_HISTORY;i++){history_user[i]=NULL;history_asst[i]=NULL;}
        history_count=keep;
        history_user[0]=strdup(s); free(history_asst[0]); history_asst[0]=strdup("OK");
    }
    history_user[history_count]=u?strdup(u):strdup("");
    history_asst[history_count]=a?strdup(a):strdup("");
    history_count++;
    history_save_to_sd();
}

/* ---- LLM ---- */
static char *llm_chat(const char *question)
{
    if (!question) { printf("LLM: null q\n"); return NULL; }
    char *body = malloc(BODY_MAX);
    if (!body) { printf("LLM: no mem\n"); return NULL; }

    char e1[2048], e2[2048];
    int off = snprintf(body, BODY_MAX,
        "{\"model\":\"deepseek-chat\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"}",
        json_esc(SYSTEM_PROMPT, e1, sizeof(e1)));

    for (int i=0; i<history_count && off<BODY_MAX-4096; i++) {
        if (history_user[i])
            off += snprintf(body+off, BODY_MAX-off,
                ",{\"role\":\"user\",\"content\":\"%s\"}", json_esc(history_user[i], e1, sizeof(e1)));
        if (history_asst[i] && strcmp(history_asst[i],"OK"))
            off += snprintf(body+off, BODY_MAX-off,
                ",{\"role\":\"assistant\",\"content\":\"%s\"}", json_esc(history_asst[i], e2, sizeof(e2)));
    }
    off += snprintf(body+off, BODY_MAX-off,
        ",{\"role\":\"user\",\"content\":\"%s\"}]}", json_esc(question, e1, sizeof(e1)));

    uint8_t *buf = malloc(16384);
    if (!buf) { free(body); printf("LLM: no buf\n"); return NULL; }
    int len, status;
    http_post("https://api.deepseek.com/v1/chat/completions",
              body, "Authorization", "Bearer " DEEPSEEK_API_KEY, NULL, NULL,
              buf, 16384, &len, &status);
    free(body);
    printf("LLM: s=%d l=%d\n", status, len);

    char *reply = NULL;
    if (status == 200 && len > 0) {
        cJSON *root = cJSON_Parse((char*)buf);
        if (root) {
            cJSON *j = cJSON_GetObjectItem(root, "choices");
            if (j) j = cJSON_GetArrayItem(j, 0);
            if (j) j = cJSON_GetObjectItem(j, "message");
            if (j) j = cJSON_GetObjectItem(j, "content");
            if (j && j->valuestring) reply = strdup(j->valuestring);
            cJSON_Delete(root);
        }
    }
    free(buf);
    return reply;
}

/* ---- UI ---- */
static lv_obj_t *root, *conv_area, *status_lbl;
static int conv_y = 5;
static volatile int ask_state = 0;
static char *ask_result_text = NULL;
extern const lv_font_t lv_font_simsun_16_cjk;

static void add_msg(const char *pfx, const char *txt, uint32_t color)
{
    lv_obj_t *l = lv_label_create(conv_area);
    lv_label_set_text_fmt(l, "%s%s", pfx, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_width(l, 310);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 10, conv_y);
    conv_y += lv_obj_get_height(l) + 10;
    lv_obj_scroll_to_y(conv_area, conv_y-180, LV_ANIM_ON);
}

static void ask_bg_task(void *arg)
{
    char *q = (char*)arg;
    printf("VOICE: ask wifi=%d heap=%lu\n", sys_wifi_is_connected(), esp_get_free_heap_size());
    char *reply = llm_chat(q);
    if (reply) { history_add(q, reply); ask_result_text=reply; ask_state=2; }
    else { ask_state=-1; printf("VOICE: fail\n"); }
    free(q);
    vTaskDelete(NULL);
}

static void ask_result_cb(lv_timer_t *t)
{
    if (ask_state==0 || ask_state==1) return;
    lv_timer_del(t);
    if (ask_state==2 && ask_result_text) {
        add_msg("AI: ", ask_result_text, 0x44cc88);
        free(ask_result_text); ask_result_text=NULL;
    } else add_msg("AI: ", "(error)", 0xcc4444);
    ask_state=0;
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
}

static void ask(const char *q)
{
    if (ask_state) return;
    add_msg("You: ", q, 0x4488cc);
    lv_label_set_text(status_lbl, "Thinking...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);
    ask_state=1;
    char *s = strdup(q); if (!s) { ask_state=-1; return; }
    xTaskCreate(ask_bg_task, "voice_bg", 16384, s, 3, NULL);
    lv_timer_create(ask_result_cb, 300, NULL);
}

static void on_q1(lv_event_t *e) { ask("Hi, who are you?"); }
static void on_q2(lv_event_t *e) { ask("Tell me a short story"); }
static void on_q3(lv_event_t *e) { ask("What is 1 plus 1?"); }

static void create(lv_obj_t *parent)
{
    conv_y=5; history_load_from_sd();
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "Voice AI | Hi Reddy!");
    lv_obj_set_style_text_font(t, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8888cc), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 5);

    status_lbl = lv_label_create(root);
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(status_lbl, LV_ALIGN_TOP_RIGHT, -10, 8);

    conv_area = lv_obj_create(root);
    lv_obj_set_size(conv_area, 330, 200);
    lv_obj_align(conv_area, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_bg_color(conv_area, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_width(conv_area, 0, 0);
    lv_obj_set_style_pad_all(conv_area, 5, 0);
    lv_obj_set_scroll_dir(conv_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(conv_area, LV_SCROLLBAR_MODE_OFF);

    static const char *qs[]={"Who r u?","A story","1+1=?"};
    static lv_event_cb_t cbs[]={on_q1,on_q2,on_q3};
    for(int i=0;i<3;i++){
        lv_obj_t *btn=lv_btn_create(root); lv_obj_set_size(btn,105,36);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 8+i*112, -8);
        lv_obj_t *bl=lv_label_create(btn); lv_label_set_text(bl,qs[i]); lv_obj_center(bl);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
    }
}

static void destroy(void)
{
    if (root) { lv_obj_del(root); root=NULL; }
    for (int i=0;i<history_count;i++){free(history_user[i]);free(history_asst[i]);}
    history_count=0;
}
static void resume(void) {}

const app_entry_t app_voice = {
    .name = "Voice AI",
    .create = create, .destroy = destroy, .resume = resume,
};
