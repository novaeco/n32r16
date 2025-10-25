#include "ota_update.h"

#include "unity.h"

#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OTA_VERSION_LEN (sizeof(((esp_app_desc_t *)0)->version))

typedef struct {
    int begin_calls;
    int perform_calls;
    int finish_calls;
    int abort_calls;
    int attempt_index;
    int fail_begin_attempts;
    int perform_cycles_before_complete;
    esp_err_t perform_result;
    esp_err_t finish_result;
    bool complete_data;
    esp_app_desc_t new_desc;
} ota_stub_state_t;

static ota_stub_state_t s_ota_state;
static esp_app_desc_t s_running_desc;
static esp_partition_t s_running_partition;
static int s_mark_valid_calls;
static esp_err_t s_mark_valid_result;
static bool s_connectivity_ready;
static int s_connectivity_checks;
static bool s_task_deleted;
static int s_restart_calls;
static bool s_validate_called;
static esp_err_t s_validate_result;
static uint8_t s_expected_sha[32];
static char s_nvs_value[OTA_VERSION_LEN + 1];
static bool s_nvs_has_value;
static char s_last_nvs_namespace[16];
static char s_last_nvs_key[16];
static esp_err_t s_nvs_open_result;
static esp_err_t s_nvs_get_result_override;
static uint32_t s_delay_ms[16];
static size_t s_delay_count;

static void reset_stubs(void)
{
    memset(&s_ota_state, 0, sizeof(s_ota_state));
    memset(&s_running_desc, 0, sizeof(s_running_desc));
    memset(&s_running_partition, 0, sizeof(s_running_partition));
    memset(s_nvs_value, 0, sizeof(s_nvs_value));
    memset(s_last_nvs_namespace, 0, sizeof(s_last_nvs_namespace));
    memset(s_last_nvs_key, 0, sizeof(s_last_nvs_key));
    memset(s_delay_ms, 0, sizeof(s_delay_ms));
    memset(s_expected_sha, 0, sizeof(s_expected_sha));
    s_mark_valid_calls = 0;
    s_mark_valid_result = ESP_OK;
    s_connectivity_ready = true;
    s_connectivity_checks = 0;
    s_task_deleted = false;
    s_restart_calls = 0;
    s_validate_called = false;
    s_validate_result = ESP_OK;
    s_nvs_has_value = false;
    s_nvs_open_result = ESP_OK;
    s_nvs_get_result_override = ESP_OK;
    s_delay_count = 0;
    s_ota_state.complete_data = true;
    s_ota_state.perform_result = ESP_OK;
    s_ota_state.finish_result = ESP_OK;
}

static bool wait_for_network(uint32_t timeout_ms)
{
    (void)timeout_ms;
    ++s_connectivity_checks;
    return s_connectivity_ready;
}

static esp_err_t validate_cb(const esp_app_desc_t *new_app, const esp_app_desc_t *running_app,
                             const uint8_t new_app_sha256[32], const char *stored_version, void *ctx)
{
    (void)ctx;
    s_validate_called = true;
    TEST_ASSERT_NOT_NULL(new_app);
    TEST_ASSERT_NOT_NULL(running_app);
    TEST_ASSERT_NULL(stored_version);
    TEST_ASSERT_EQUAL_STRING(s_running_desc.version, running_app->version);
    TEST_ASSERT_EQUAL_STRING(s_ota_state.new_desc.version, new_app->version);
    TEST_ASSERT_EQUAL_MEMORY(s_expected_sha, new_app_sha256, sizeof(s_expected_sha));
    return s_validate_result;
}

void vTaskDelay(const TickType_t ticks)
{
    if (s_delay_count < sizeof(s_delay_ms) / sizeof(s_delay_ms[0])) {
        s_delay_ms[s_delay_count++] = (uint32_t)(ticks * portTICK_PERIOD_MS);
    }
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    s_task_deleted = true;
}

void esp_restart(void)
{
    ++s_restart_calls;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    ++s_mark_valid_calls;
    return s_mark_valid_result;
}

const esp_partition_t *esp_ota_get_running_partition(void)
{
    return &s_running_partition;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *out_desc)
{
    TEST_ASSERT_EQUAL_PTR(&s_running_partition, partition);
    *out_desc = s_running_desc;
    return ESP_OK;
}

esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *config, esp_https_ota_handle_t *handle)
{
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_NOT_NULL(handle);
    ++s_ota_state.begin_calls;
    ++s_ota_state.attempt_index;
    if (s_ota_state.attempt_index <= s_ota_state.fail_begin_attempts) {
        return ESP_FAIL;
    }
    s_ota_state.perform_calls = 0;
    *handle = (esp_https_ota_handle_t)&s_ota_state;
    return ESP_OK;
}

esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t handle, esp_app_desc_t *out_desc)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_state, (void *)handle);
    *out_desc = s_ota_state.new_desc;
    return ESP_OK;
}

esp_err_t esp_https_ota_perform(esp_https_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_state, (void *)handle);
    ++s_ota_state.perform_calls;
    if (s_ota_state.perform_calls <= s_ota_state.perform_cycles_before_complete) {
        return ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    }
    return s_ota_state.perform_result;
}

bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_state, (void *)handle);
    return s_ota_state.complete_data;
}

esp_err_t esp_https_ota_finish(esp_https_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_state, (void *)handle);
    ++s_ota_state.finish_calls;
    return s_ota_state.finish_result;
}

void esp_https_ota_abort(esp_https_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_state, (void *)handle);
    ++s_ota_state.abort_calls;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    (void)open_mode;
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_NOT_NULL(out_handle);
    if (s_nvs_open_result != ESP_OK) {
        return s_nvs_open_result;
    }
    strlcpy(s_last_nvs_namespace, name, sizeof(s_last_nvs_namespace));
    *out_handle = (nvs_handle_t)0x1;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length)
{
    TEST_ASSERT_EQUAL((nvs_handle_t)0x1, handle);
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_NOT_NULL(length);
    if (s_nvs_get_result_override != ESP_OK) {
        return s_nvs_get_result_override;
    }
    if (!s_nvs_has_value) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    strlcpy(s_last_nvs_key, key, sizeof(s_last_nvs_key));
    TEST_ASSERT_TRUE(*length >= strlen(s_nvs_value) + 1);
    strlcpy(out_value, s_nvs_value, *length);
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    TEST_ASSERT_EQUAL((nvs_handle_t)0x1, handle);
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_NOT_NULL(value);
    strlcpy(s_last_nvs_key, key, sizeof(s_last_nvs_key));
    strlcpy(s_nvs_value, value, sizeof(s_nvs_value));
    s_nvs_has_value = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    TEST_ASSERT_EQUAL((nvs_handle_t)0x1, handle);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    TEST_ASSERT_EQUAL((nvs_handle_t)0x1, handle);
}

static ota_update_config_t *alloc_config(void)
{
    ota_update_config_t *cfg = (ota_update_config_t *)calloc(1, sizeof(*cfg));
    TEST_ASSERT_NOT_NULL(cfg);
    cfg->url = "https://example.com/ota.bin";
    cfg->cert_pem = (const uint8_t *)"CA";
    cfg->cert_len = 2;
    cfg->auto_reboot = false;
    cfg->check_delay_ms = 0;
    cfg->initial_backoff_ms = 100;
    cfg->max_backoff_ms = 1000;
    cfg->max_retries = 3;
    cfg->wait_for_connectivity = wait_for_network;
    return cfg;
}

TEST_CASE("ota update persists version and reboots on success", "[ota]")
{
    reset_stubs();
    strlcpy(s_running_desc.version, "v1.0.0", sizeof(s_running_desc.version));
    strlcpy(s_ota_state.new_desc.version, "v2.0.1", sizeof(s_ota_state.new_desc.version));
    for (size_t i = 0; i < sizeof(s_expected_sha); ++i) {
        s_expected_sha[i] = (uint8_t)(i + 1U);
        s_ota_state.new_desc.app_elf_sha256[i] = (char)s_expected_sha[i];
    }
    s_ota_state.perform_cycles_before_complete = 1;
    s_ota_state.perform_result = ESP_OK;
    s_ota_state.finish_result = ESP_OK;

    ota_update_config_t *cfg = alloc_config();
    cfg->auto_reboot = true;
    cfg->validate = validate_cb;
    cfg->validate_ctx = NULL;

    ota_update_task_entry(cfg);

    TEST_ASSERT_TRUE(s_task_deleted);
    TEST_ASSERT_TRUE(s_validate_called);
    TEST_ASSERT_EQUAL(1, s_restart_calls);
    TEST_ASSERT_TRUE(s_nvs_has_value);
    TEST_ASSERT_EQUAL_STRING("v2.0.1", s_nvs_value);
    TEST_ASSERT_EQUAL_STRING("ota_state", s_last_nvs_namespace);
    TEST_ASSERT_EQUAL_STRING("last_version", s_last_nvs_key);
    TEST_ASSERT_EQUAL(1, s_ota_state.begin_calls);
    TEST_ASSERT_EQUAL(1, s_ota_state.finish_calls);
    TEST_ASSERT_EQUAL(0, s_ota_state.abort_calls);
    TEST_ASSERT_TRUE(s_delay_count >= 2);
    TEST_ASSERT_EQUAL(1000, s_delay_ms[s_delay_count - 1]);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_mark_valid_calls);
}

TEST_CASE("ota update retries with exponential backoff", "[ota]")
{
    reset_stubs();
    s_ota_state.fail_begin_attempts = 2;
    strlcpy(s_ota_state.new_desc.version, "v1.2.3", sizeof(s_ota_state.new_desc.version));
    strlcpy(s_running_desc.version, "v1.0.0", sizeof(s_running_desc.version));

    ota_update_config_t *cfg = alloc_config();
    cfg->initial_backoff_ms = 500;
    cfg->max_backoff_ms = 2000;
    cfg->max_retries = 4;

    ota_update_task_entry(cfg);

    TEST_ASSERT_TRUE(s_task_deleted);
    TEST_ASSERT_EQUAL(3, s_ota_state.begin_calls);
    TEST_ASSERT_EQUAL(1, s_ota_state.finish_calls);
    TEST_ASSERT_TRUE(s_nvs_has_value);
    TEST_ASSERT_EQUAL_STRING("v1.2.3", s_nvs_value);
    TEST_ASSERT_EQUAL(2, (int)s_delay_count);
    TEST_ASSERT_EQUAL(500, s_delay_ms[0]);
    TEST_ASSERT_EQUAL(1000, s_delay_ms[1]);
}

TEST_CASE("ota update aborts on validation failure", "[ota]")
{
    reset_stubs();
    strlcpy(s_running_desc.version, "v1.0.0", sizeof(s_running_desc.version));
    strlcpy(s_ota_state.new_desc.version, "v2.0.0", sizeof(s_ota_state.new_desc.version));
    s_validate_result = ESP_ERR_INVALID_ARG;

    ota_update_config_t *cfg = alloc_config();
    cfg->validate = validate_cb;

    ota_update_task_entry(cfg);

    TEST_ASSERT_TRUE(s_task_deleted);
    TEST_ASSERT_TRUE(s_validate_called);
    TEST_ASSERT_EQUAL(1, s_ota_state.begin_calls);
    TEST_ASSERT_EQUAL(0, s_ota_state.finish_calls);
    TEST_ASSERT_EQUAL(1, s_ota_state.abort_calls);
    TEST_ASSERT_FALSE(s_nvs_has_value);
    TEST_ASSERT_EQUAL(0, s_restart_calls);
    TEST_ASSERT_EQUAL(0, (int)s_delay_count);
}
