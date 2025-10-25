#include "mdns_helper.h"

#include "unity.h"
#include <string.h>

static int s_init_calls;
static int s_hostname_calls;
static int s_instance_calls;
static int s_service_add_calls;
static int s_txt_proto_calls;
static int s_txt_auth_calls;
static int s_free_calls;
static esp_err_t s_init_result;
static const char *s_last_hostname;
static const char *s_last_instance;
static uint16_t s_last_port;

static esp_err_t stub_mdns_init(void)
{
    ++s_init_calls;
    return s_init_result;
}

static esp_err_t stub_mdns_hostname_set(const char *hostname)
{
    ++s_hostname_calls;
    s_last_hostname = hostname;
    return ESP_OK;
}

static esp_err_t stub_mdns_instance_name_set(const char *instance)
{
    ++s_instance_calls;
    s_last_instance = instance;
    return ESP_OK;
}

static esp_err_t stub_mdns_service_add(const char *instance, const char *service_type, const char *proto,
                                       uint16_t port, const mdns_txt_item_t *txt, size_t num_items)
{
    (void)instance;
    (void)service_type;
    (void)proto;
    (void)txt;
    (void)num_items;
    ++s_service_add_calls;
    s_last_port = port;
    return ESP_OK;
}

static esp_err_t stub_mdns_txt_router(const char *service_type, const char *proto, const char *key,
                                      const char *value)
{
    (void)service_type;
    (void)proto;
    (void)value;
    if (key && strcmp(key, "proto") == 0) {
        ++s_txt_proto_calls;
    } else if (key && strcmp(key, "auth") == 0) {
        ++s_txt_auth_calls;
    }
    return ESP_OK;
}

static void stub_mdns_free(void)
{
    ++s_free_calls;
}

static const mdns_helper_platform_t s_stub_platform = {
    .mdns_init = stub_mdns_init,
    .mdns_hostname_set = stub_mdns_hostname_set,
    .mdns_instance_name_set = stub_mdns_instance_name_set,
    .mdns_service_add = stub_mdns_service_add,
    .mdns_service_txt_item_set = stub_mdns_txt_router,
    .mdns_free = stub_mdns_free,
};

static mdns_helper_platform_t s_override_platform;

static void reset_state(void)
{
    s_init_calls = 0;
    s_hostname_calls = 0;
    s_instance_calls = 0;
    s_service_add_calls = 0;
    s_txt_proto_calls = 0;
    s_txt_auth_calls = 0;
    s_free_calls = 0;
    s_init_result = ESP_OK;
    s_last_hostname = NULL;
    s_last_instance = NULL;
    s_last_port = 0;
}

void setUp(void)
{
    reset_state();
    s_override_platform = s_stub_platform;
    mdns_helper_set_platform(&s_override_platform);
}

void tearDown(void)
{
    mdns_helper_set_platform(NULL);
}

TEST_CASE("mdns helper starts service and sets labels", "[net][mdns]")
{
    esp_err_t err = mdns_helper_start("panel", "test-instance", 3210);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, s_init_calls);
    TEST_ASSERT_EQUAL_STRING("panel", s_last_hostname);
    TEST_ASSERT_EQUAL_STRING("test-instance", s_last_instance);
    TEST_ASSERT_EQUAL_UINT16(3210, s_last_port);
    TEST_ASSERT_EQUAL(1, s_txt_proto_calls);
    TEST_ASSERT_EQUAL(1, s_txt_auth_calls);
    mdns_helper_stop();
    TEST_ASSERT_EQUAL(1, s_free_calls);
}

TEST_CASE("mdns helper propagates init failure", "[net][mdns]")
{
    s_init_result = ESP_FAIL;
    esp_err_t err = mdns_helper_start(NULL, NULL, 0);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, s_init_calls);
    TEST_ASSERT_EQUAL(0, s_service_add_calls);
}
