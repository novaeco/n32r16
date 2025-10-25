#include "wifi_manager.h"

#include "unity.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "wifi_provisioning/scheme_softap.h"
#include <string.h>

static wifi_prov_mgr_config_t s_cfg_history[4];
static int s_init_calls;
static esp_err_t s_next_ble_err;
static esp_err_t s_next_softap_err;

esp_err_t wifi_manager_prov_mgr_init(const wifi_prov_mgr_config_t config)
{
    if (s_init_calls < (int)(sizeof(s_cfg_history) / sizeof(s_cfg_history[0]))) {
        s_cfg_history[s_init_calls] = config;
    }
    ++s_init_calls;
    if (config.scheme == wifi_prov_scheme_ble && s_next_ble_err != ESP_OK) {
        esp_err_t err = s_next_ble_err;
        s_next_ble_err = ESP_OK;
        return err;
    }
    if (config.scheme == wifi_prov_scheme_softap && s_next_softap_err != ESP_OK) {
        esp_err_t err = s_next_softap_err;
        s_next_softap_err = ESP_OK;
        return err;
    }
    return ESP_OK;
}

static void reset_state(void)
{
    memset(s_cfg_history, 0, sizeof(s_cfg_history));
    s_init_calls = 0;
    s_next_ble_err = ESP_OK;
    s_next_softap_err = ESP_OK;
}

void setUp(void)
{
    reset_state();
}

void tearDown(void)
{
}

void test_wifi_manager_prepare_provisioning_softap_default(void)
{
    wifi_manager_config_t cfg = {0};
    cfg.prefer_ble = false;

    bool used_ble = true;
    esp_err_t err = wifi_manager_prepare_provisioning(&cfg, &used_ble);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, s_init_calls);
    TEST_ASSERT_EQUAL_PTR(&wifi_prov_scheme_softap, s_cfg_history[0].scheme);
    TEST_ASSERT_FALSE(used_ble);
}

void test_wifi_manager_prepare_provisioning_ble_success(void)
{
    wifi_manager_config_t cfg = {0};
    cfg.prefer_ble = true;

    bool used_ble = false;
    esp_err_t err = wifi_manager_prepare_provisioning(&cfg, &used_ble);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, s_init_calls);
    TEST_ASSERT_EQUAL_PTR(&wifi_prov_scheme_ble, s_cfg_history[0].scheme);
    TEST_ASSERT_TRUE(used_ble);
}

void test_wifi_manager_prepare_provisioning_ble_fallback_softap(void)
{
    wifi_manager_config_t cfg = {0};
    cfg.prefer_ble = true;
    s_next_ble_err = ESP_ERR_NOT_SUPPORTED;

    bool used_ble = true;
    esp_err_t err = wifi_manager_prepare_provisioning(&cfg, &used_ble);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(2, s_init_calls);
    TEST_ASSERT_EQUAL_PTR(&wifi_prov_scheme_ble, s_cfg_history[0].scheme);
    TEST_ASSERT_EQUAL_PTR(&wifi_prov_scheme_softap, s_cfg_history[1].scheme);
    TEST_ASSERT_FALSE(used_ble);
}

void test_wifi_manager_prepare_provisioning_softap_failure_propagates(void)
{
    wifi_manager_config_t cfg = {0};
    cfg.prefer_ble = false;
    s_next_softap_err = ESP_FAIL;

    esp_err_t err = wifi_manager_prepare_provisioning(&cfg, NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, s_init_calls);
    TEST_ASSERT_EQUAL_PTR(&wifi_prov_scheme_softap, s_cfg_history[0].scheme);
}

void test_wifi_manager_prepare_provisioning_invalid_argument(void)
{
    esp_err_t err = wifi_manager_prepare_provisioning(NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL(0, s_init_calls);
}
