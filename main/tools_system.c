#include "tools_handlers.h"
#include "config.h"
#include "ota.h"
#include "ratelimit.h"
#include "cron.h"
#include "user_tools.h"
#include "esp_system.h"
#include <stdio.h>

bool tools_get_version_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;
    snprintf(result, result_len, "zclaw v%s", ota_get_version());
    return true;
}

bool tools_get_health_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    // Get heap info
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();

    // Get rate limit info
    int requests_hour = ratelimit_get_requests_this_hour();
    int requests_day = ratelimit_get_requests_today();

    // Get time sync status
    bool time_synced = cron_is_time_synced();
    char timezone_posix[TIMEZONE_MAX_LEN];
    char timezone_abbrev[16];
    cron_get_timezone(timezone_posix, sizeof(timezone_posix));
    cron_get_timezone_abbrev(timezone_abbrev, sizeof(timezone_abbrev));

    snprintf(result, result_len,
             "Health: OK | "
             "Heap: %lu free, %lu min | "
             "Requests: %d/hr, %d/day | "
             "Time: %s | "
             "TZ: %s (%s) | "
             "Version: %s",
             (unsigned long)free_heap,
             (unsigned long)min_heap,
             requests_hour,
             requests_day,
             time_synced ? "synced" : "not synced",
             timezone_posix,
             timezone_abbrev,
             ota_get_version());

    return true;
}

bool tools_create_tool_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *name_json = cJSON_GetObjectItem(input, "name");
    cJSON *desc_json = cJSON_GetObjectItem(input, "description");
    cJSON *action_json = cJSON_GetObjectItem(input, "action");

    if (!name_json || !cJSON_IsString(name_json)) {
        snprintf(result, result_len, "Error: 'name' required (string, no spaces)");
        return false;
    }
    if (!desc_json || !cJSON_IsString(desc_json)) {
        snprintf(result, result_len, "Error: 'description' required (short description)");
        return false;
    }
    if (!action_json || !cJSON_IsString(action_json)) {
        snprintf(result, result_len, "Error: 'action' required (what to do when called)");
        return false;
    }

    const char *name = name_json->valuestring;
    const char *description = desc_json->valuestring;
    const char *action = action_json->valuestring;

    // Validate name: no spaces, alphanumeric + underscore
    for (size_t i = 0; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            snprintf(result, result_len, "Error: name must be alphanumeric/underscore, no spaces");
            return false;
        }
    }

    if (user_tools_create(name, description, action)) {
        snprintf(result, result_len, "Created tool '%s': %s", name, description);
        return true;
    }

    snprintf(result, result_len, "Error: failed to create tool (duplicate or limit reached)");
    return false;
}

bool tools_list_user_tools_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;
    user_tools_list(result, result_len);
    return true;
}

bool tools_delete_user_tool_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *name_json = cJSON_GetObjectItem(input, "name");

    if (!name_json || !cJSON_IsString(name_json)) {
        snprintf(result, result_len, "Error: 'name' required");
        return false;
    }

    if (user_tools_delete(name_json->valuestring)) {
        snprintf(result, result_len, "Deleted tool '%s'", name_json->valuestring);
        return true;
    }

    snprintf(result, result_len, "Tool '%s' not found", name_json->valuestring);
    return true;
}
