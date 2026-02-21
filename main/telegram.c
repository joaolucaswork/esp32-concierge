#include "telegram.h"
#include "config.h"
#include "messages.h"
#include "memory.h"
#include "nvs_keys.h"
#include "telegram_update.h"
#include "text_buffer.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

static const char *TAG = "telegram";

static QueueHandle_t s_input_queue;
static QueueHandle_t s_output_queue;
static char s_bot_token[64] = {0};
static int64_t s_chat_id = 0;
static int64_t s_last_update_id = 0;
static telegram_msg_t s_send_msg;

// Nano newlib printf doesn't support %lld/PRId64. This helper converts int64 to string.
static const char *i64_to_str(int64_t val, char *buf, size_t buf_size)
{
    if (buf_size == 0) return "";
    bool negative = (val < 0);
    uint64_t uval = negative ? (uint64_t)(-(val + 1)) + 1 : (uint64_t)val;
    char *p = buf + buf_size - 1;
    *p = '\0';
    do {
        if (p == buf) return "0";  // overflow guard
        *(--p) = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);
    if (negative) {
        if (p == buf) return "0";
        *(--p) = '-';
    }
    return p;
}

// Exponential backoff state
static int s_consecutive_failures = 0;
#define BACKOFF_BASE_MS     5000    // 5 seconds
#define BACKOFF_MAX_MS      300000  // 5 minutes
#define BACKOFF_MULTIPLIER  2

typedef struct {
    char buf[4096];
    size_t len;
    bool truncated;
} telegram_http_ctx_t;

static bool parse_chat_id_string(const char *input, int64_t *chat_id_out)
{
    const unsigned char *cursor = (const unsigned char *)input;
    char *endptr = NULL;
    long long parsed;

    if (!input || !chat_id_out) {
        return false;
    }

    while (*cursor != '\0' && isspace(*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        return false;
    }

    parsed = strtoll((const char *)cursor, &endptr, 10);
    if (!endptr || endptr == (const char *)cursor) {
        return false;
    }

    while (*endptr != '\0' && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    if (*endptr != '\0' || parsed == 0) {
        return false;
    }

    *chat_id_out = (int64_t)parsed;
    return true;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    telegram_http_ctx_t *ctx = (telegram_http_ctx_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (ctx) {
                bool ok = text_buffer_append(ctx->buf, &ctx->len, sizeof(ctx->buf),
                                             (const char *)evt->data, evt->data_len);
                if (!ok && !ctx->truncated) {
                    ctx->truncated = true;
                    ESP_LOGW(TAG, "Telegram HTTP response truncated");
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t telegram_init(void)
{
    // Load bot token from NVS
    if (!memory_get(NVS_KEY_TG_TOKEN, s_bot_token, sizeof(s_bot_token))) {
        ESP_LOGW(TAG, "No Telegram token configured");
        return ESP_ERR_NOT_FOUND;
    }

    // Load last known chat ID (optional)
    char chat_id_str[24];
    if (memory_get(NVS_KEY_TG_CHAT_ID, chat_id_str, sizeof(chat_id_str))) {
        int64_t parsed_chat_id = 0;
        if (parse_chat_id_string(chat_id_str, &parsed_chat_id)) {
            s_chat_id = parsed_chat_id;
            char id_buf[24];
            ESP_LOGI(TAG, "Loaded chat ID: %s", i64_to_str(s_chat_id, id_buf, sizeof(id_buf)));
        } else {
            s_chat_id = 0;
            ESP_LOGW(TAG, "Invalid Telegram chat ID in NVS: '%s'", chat_id_str);
        }
    }

    ESP_LOGI(TAG, "Telegram initialized");
    return ESP_OK;
}

bool telegram_is_configured(void)
{
    return s_bot_token[0] != '\0';
}

int64_t telegram_get_chat_id(void)
{
    return s_chat_id;
}

// Build URL for Telegram API
static void build_url(char *buf, size_t buf_size, const char *method)
{
    snprintf(buf, buf_size, "%s%s/%s", TELEGRAM_API_URL, s_bot_token, method);
}

esp_err_t telegram_send(const char *text)
{
    telegram_http_ctx_t *ctx = NULL;
    esp_http_client_handle_t client = NULL;
    esp_err_t err;

    if (!telegram_is_configured() || s_chat_id == 0) {
        ESP_LOGW(TAG, "Cannot send - not configured or no chat ID");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    build_url(url, sizeof(url), "sendMessage");

    // Build JSON body
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    if (!cJSON_AddNumberToObject(root, "chat_id", (double)s_chat_id) ||
        !cJSON_AddStringToObject(root, "text", text)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = ctx,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(body);
        free(ctx);
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "sendMessage failed: %d", status);
            if (ctx->buf[0] != '\0') {
                ESP_LOGE(TAG, "sendMessage response: %s", ctx->buf);
            }
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    free(body);
    free(ctx);
    return err;
}

esp_err_t telegram_send_startup(void)
{
    return telegram_send("I'm back online. What can I help you with?");
}

// Poll for updates using long polling
static esp_err_t telegram_poll(void)
{
    char url[384];
    telegram_http_ctx_t *ctx = NULL;
    esp_http_client_handle_t client = NULL;
    esp_err_t err;
    int status;

    char off_buf[24];
    snprintf(url, sizeof(url), "%s%s/getUpdates?timeout=%d&limit=1&offset=%s",
             TELEGRAM_API_URL, s_bot_token, TELEGRAM_POLL_TIMEOUT,
             i64_to_str(s_last_update_id + 1, off_buf, sizeof(off_buf)));

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = ctx,
        .timeout_ms = (TELEGRAM_POLL_TIMEOUT + 10) * 1000,  // Add buffer to timeout
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for poll");
        free(ctx);
        return ESP_FAIL;
    }

    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    client = NULL;

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "getUpdates failed: err=%d status=%d", err, status);
        free(ctx);
        return ESP_FAIL;
    }

    if (ctx->truncated) {
        int64_t recovered_update_id = 0;
        if (telegram_extract_max_update_id(ctx->buf, &recovered_update_id)) {
            s_last_update_id = recovered_update_id;
            char rec_buf[24];
            ESP_LOGW(TAG, "Recovered from truncated response, skipping to update_id=%s",
                     i64_to_str(s_last_update_id, rec_buf, sizeof(rec_buf)));
            free(ctx);
            return ESP_OK;
        }

        ESP_LOGE(TAG, "Truncated response without parseable update_id");
        free(ctx);
        return ESP_FAIL;
    }

    // Parse response
    cJSON *root = cJSON_Parse(ctx->buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse response");
        free(ctx);
        return ESP_FAIL;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!ok || !cJSON_IsTrue(ok)) {
        ESP_LOGE(TAG, "API returned not ok");
        cJSON_Delete(root);
        free(ctx);
        return ESP_FAIL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsArray(result)) {
        cJSON_Delete(root);
        free(ctx);
        return ESP_OK;  // No updates, that's fine
    }

    cJSON *update;
    cJSON_ArrayForEach(update, result) {
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        if (update_id && cJSON_IsNumber(update_id)) {
            // Note: cJSON stores numbers as double (53-bit precision).
            // Telegram update IDs fit safely within this range.
            s_last_update_id = (int64_t)update_id->valuedouble;
        }

        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!message) continue;

        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        cJSON *text = cJSON_GetObjectItem(message, "text");

        if (chat && text && cJSON_IsString(text)) {
            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            if (chat_id && cJSON_IsNumber(chat_id)) {
                // Note: cJSON stores numbers as double (53-bit precision)
                // Telegram chat IDs fit within this range
                int64_t incoming_chat_id = (int64_t)chat_id->valuedouble;

                // Sanity check for precision loss (chat IDs > 2^53)
                if (chat_id->valuedouble > 9007199254740992.0) {
                    ESP_LOGW(TAG, "Chat ID may have precision loss");
                }

                // Authentication: reject messages from unknown chat IDs
                if (s_chat_id != 0 && incoming_chat_id != s_chat_id) {
                    char rej_buf[24];
                    ESP_LOGW(TAG, "Rejected message from unauthorized chat: %s",
                             i64_to_str(incoming_chat_id, rej_buf, sizeof(rej_buf)));
                    continue;
                }

                // If no chat ID configured, reject all (must be set during provisioning)
                if (s_chat_id == 0) {
                    char ign_buf[24];
                    ESP_LOGW(TAG, "No chat ID configured - ignoring message from %s",
                             i64_to_str(incoming_chat_id, ign_buf, sizeof(ign_buf)));
                    continue;
                }

                // Push message to input queue
                channel_msg_t msg;
                strncpy(msg.text, text->valuestring, CHANNEL_RX_BUF_SIZE - 1);
                msg.text[CHANNEL_RX_BUF_SIZE - 1] = '\0';

                ESP_LOGI(TAG, "Received: %s", msg.text);

                if (xQueueSend(s_input_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                    ESP_LOGW(TAG, "Input queue full");
                }
            }
        }
    }

    cJSON_Delete(root);
    free(ctx);
    return ESP_OK;
}

// Telegram response task - watches output queue, sends to Telegram
static void telegram_send_task(void *arg)
{
    (void)arg;
    while (1) {
        if (xQueueReceive(s_output_queue, &s_send_msg, portMAX_DELAY) == pdTRUE) {
            if (telegram_is_configured() && s_chat_id != 0) {
                telegram_send(s_send_msg.text);
            }
        }
    }
}

// Calculate exponential backoff delay
static int get_backoff_delay_ms(void)
{
    if (s_consecutive_failures == 0) {
        return 0;
    }

    int delay = BACKOFF_BASE_MS;
    for (int i = 1; i < s_consecutive_failures && delay < BACKOFF_MAX_MS; i++) {
        delay *= BACKOFF_MULTIPLIER;
    }

    if (delay > BACKOFF_MAX_MS) {
        delay = BACKOFF_MAX_MS;
    }

    return delay;
}

// Helper: single getUpdates call, returns HTTP status or -1 on error
static int telegram_get_updates_raw(const char *url, telegram_http_ctx_t *ctx)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = ctx,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    return status;
}

// Flush any pending updates so we don't reprocess old messages after reboot.
// Step 1: getUpdates?offset=-1 to get the last pending update_id.
// Step 2: getUpdates?offset=last_id+1 to confirm/acknowledge all updates.
static void telegram_flush_pending(void)
{
    char url[384];

    // Step 1: get the last pending update
    snprintf(url, sizeof(url), "%s%s/getUpdates?offset=-1&limit=1&timeout=0",
             TELEGRAM_API_URL, s_bot_token);

    telegram_http_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;

    int status = telegram_get_updates_raw(url, ctx);
    if (status != 200) {
        ESP_LOGW(TAG, "Flush step 1 failed (status=%d)", status);
        free(ctx);
        return;
    }

    // Parse the last update_id
    int64_t last_id = 0;
    cJSON *root = cJSON_Parse(ctx->buf);
    if (root) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result && cJSON_IsArray(result)) {
            cJSON *update = cJSON_GetArrayItem(result, 0);
            if (update) {
                cJSON *uid = cJSON_GetObjectItem(update, "update_id");
                if (uid && cJSON_IsNumber(uid)) {
                    last_id = (int64_t)uid->valuedouble;
                }
            }
        }
        cJSON_Delete(root);
    }
    free(ctx);

    if (last_id == 0) {
        ESP_LOGI(TAG, "No pending updates to flush");
        return;
    }

    // Step 2: confirm all updates by requesting offset = last_id + 1
    char flush_buf[24];
    snprintf(url, sizeof(url), "%s%s/getUpdates?offset=%s&limit=1&timeout=0",
             TELEGRAM_API_URL, s_bot_token,
             i64_to_str(last_id + 1, flush_buf, sizeof(flush_buf)));

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;

    status = telegram_get_updates_raw(url, ctx);
    free(ctx);

    s_last_update_id = last_id;
    char fid_buf[24];
    ESP_LOGI(TAG, "Flushed all pending updates up to %s (confirm status=%d)",
             i64_to_str(last_id, fid_buf, sizeof(fid_buf)), status);
}

// Telegram polling task - polls for new messages
static void telegram_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Polling task started");

    // Discard old messages from before this boot
    telegram_flush_pending();

    while (1) {
        if (telegram_is_configured()) {
            esp_err_t err = telegram_poll();
            if (err != ESP_OK) {
                s_consecutive_failures++;
                int backoff_ms = get_backoff_delay_ms();
                ESP_LOGW(TAG, "Poll failed (%d consecutive), backoff %dms",
                         s_consecutive_failures, backoff_ms);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            } else {
                // Success - reset backoff
                if (s_consecutive_failures > 0) {
                    ESP_LOGI(TAG, "Poll recovered after %d failures", s_consecutive_failures);
                    s_consecutive_failures = 0;
                }
            }
        } else {
            // Not configured, check again later
            vTaskDelay(pdMS_TO_TICKS(10000));
        }

        // Small delay between successful polls
        vTaskDelay(pdMS_TO_TICKS(TELEGRAM_POLL_INTERVAL));
    }
}

esp_err_t telegram_start(QueueHandle_t input_queue, QueueHandle_t output_queue)
{
    if (!input_queue || !output_queue) {
        ESP_LOGE(TAG, "Invalid queues for Telegram startup");
        return ESP_ERR_INVALID_ARG;
    }

    s_input_queue = input_queue;
    s_output_queue = output_queue;

    TaskHandle_t poll_task = NULL;
    if (xTaskCreate(telegram_poll_task, "tg_poll", CHANNEL_TASK_STACK_SIZE, NULL,
                    CHANNEL_TASK_PRIORITY, &poll_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Telegram poll task");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(telegram_send_task, "tg_send", CHANNEL_TASK_STACK_SIZE, NULL,
                    CHANNEL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Telegram send task");
        vTaskDelete(poll_task);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Telegram tasks started");
    return ESP_OK;
}
