#include "ws_client.h"

#include "unity.h"
#include <string.h>
#include "esp_idf_version.h"
#include "ws_security.h"

typedef struct {
    TimerCallbackFunction_t cb;
    TickType_t period_ticks;
    bool started;
} fake_timer_t;

static ws_client_platform_t s_platform;
static esp_websocket_client_config_t s_last_config;
static esp_event_handler_t s_event_handler;
static void *s_event_ctx;
static esp_websocket_client_handle_t s_fake_handle = (esp_websocket_client_handle_t)0x1234;
static fake_timer_t s_timer;
static bool s_stop_called;
static bool s_destroy_called;
static int s_send_calls;
static int s_timer_change_calls;
static int s_timer_start_calls;
static int s_timer_stop_calls;
static int s_timer_delete_calls;
static int s_client_start_calls;
static int s_client_init_calls;
static bool s_force_send_fail;
static uint32_t s_last_delay_ms;
static const char *s_last_uri;
static size_t s_last_send_len;
static const uint8_t *s_last_rx_payload;
static size_t s_last_rx_len;
static uint32_t s_last_rx_crc;
static int s_rx_calls;
static uint64_t s_fake_unix_time;

static uint64_t fake_time_provider(void)
{
    return s_fake_unix_time;
}

static esp_websocket_client_handle_t fake_client_init(const esp_websocket_client_config_t *config)
{
    ++s_client_init_calls;
    memcpy(&s_last_config, config, sizeof(s_last_config));
    s_last_uri = config->uri;
    return s_fake_handle;
}

static esp_err_t fake_client_start(esp_websocket_client_handle_t client)
{
    (void)client;
    ++s_client_start_calls;
    return ESP_OK;
}

static esp_err_t fake_client_stop(esp_websocket_client_handle_t client)
{
    (void)client;
    ++s_timer_stop_calls;
    s_stop_called = true;
    return ESP_OK;
}

static void fake_client_destroy(esp_websocket_client_handle_t client)
{
    (void)client;
    s_destroy_called = true;
}

static const char *fake_client_get_uri(esp_websocket_client_handle_t client)
{
    (void)client;
    return s_last_uri;
}

static int fake_client_send_bin(esp_websocket_client_handle_t client, const char *data, size_t len,
                                TickType_t timeout)
{
    (void)client;
    (void)timeout;
    ++s_send_calls;
    s_last_send_len = len;
    if (s_force_send_fail) {
        return -1;
    }
    return (int)len;
}

static esp_err_t fake_register_events(esp_websocket_client_handle_t client, esp_websocket_event_id_t event,
                                      esp_event_handler_t handler, void *handler_args)
{
    (void)client;
    (void)event;
    s_event_handler = handler;
    s_event_ctx = handler_args;
    return ESP_OK;
}

static TimerHandle_t fake_timer_create(const char *name, TickType_t period, UBaseType_t auto_reload,
                                       void *timer_id, TimerCallbackFunction_t callback)
{
    (void)name;
    (void)auto_reload;
    (void)timer_id;
    s_timer.cb = callback;
    s_timer.period_ticks = period;
    s_timer.started = false;
    return (TimerHandle_t)&s_timer;
}

static BaseType_t fake_timer_start(TimerHandle_t timer, TickType_t ticks_to_wait)
{
    (void)timer;
    (void)ticks_to_wait;
    ++s_timer_start_calls;
    s_timer.started = true;
    return pdPASS;
}

static BaseType_t fake_timer_stop(TimerHandle_t timer, TickType_t ticks)
{
    (void)timer;
    (void)ticks;
    ++s_timer_stop_calls;
    s_timer.started = false;
    return pdPASS;
}

static BaseType_t fake_timer_delete(TimerHandle_t timer, TickType_t ticks)
{
    (void)timer;
    (void)ticks;
    ++s_timer_delete_calls;
    s_timer.started = false;
    return pdPASS;
}

static BaseType_t fake_timer_change(TimerHandle_t timer, TickType_t new_period, TickType_t ticks)
{
    (void)timer;
    (void)ticks;
    ++s_timer_change_calls;
    s_timer.period_ticks = new_period;
    s_last_delay_ms = (uint32_t)(new_period * portTICK_PERIOD_MS);
    return pdPASS;
}

static void reset_platform(void)
{
    memset(&s_platform, 0, sizeof(s_platform));
    s_platform.client_init = fake_client_init;
    s_platform.client_start = fake_client_start;
    s_platform.client_stop = fake_client_stop;
    s_platform.client_destroy = fake_client_destroy;
    s_platform.client_get_uri = fake_client_get_uri;
    s_platform.client_send_bin = fake_client_send_bin;
    s_platform.register_events = fake_register_events;
    s_platform.timer_create = fake_timer_create;
    s_platform.timer_start = fake_timer_start;
    s_platform.timer_stop = fake_timer_stop;
    s_platform.timer_delete = fake_timer_delete;
    s_platform.timer_change_period = fake_timer_change;
}

static void reset_state(void)
{
    memset(&s_last_config, 0, sizeof(s_last_config));
    s_event_handler = NULL;
    s_event_ctx = NULL;
    memset(&s_timer, 0, sizeof(s_timer));
    s_stop_called = false;
    s_destroy_called = false;
    s_send_calls = 0;
    s_timer_change_calls = 0;
    s_timer_start_calls = 0;
    s_timer_stop_calls = 0;
    s_timer_delete_calls = 0;
    s_client_start_calls = 0;
    s_client_init_calls = 0;
    s_force_send_fail = false;
    s_last_delay_ms = 0;
    s_last_uri = NULL;
    s_last_send_len = 0;
    s_last_rx_payload = NULL;
    s_last_rx_len = 0;
    s_last_rx_crc = 0;
    s_rx_calls = 0;
    s_last_send_len = 0;
    s_fake_unix_time = 0;
}

void setUp(void)
{
    reset_platform();
    reset_state();
    ws_client_set_platform(&s_platform);
}

void tearDown(void)
{
    ws_client_stop();
    ws_client_set_platform(NULL);
}

static void capture_rx(const uint8_t *data, size_t len, uint32_t crc32, void *ctx)
{
    (void)ctx;
    s_last_rx_payload = data;
    s_last_rx_len = len;
    s_last_rx_crc = crc32;
    ++s_rx_calls;
}

TEST_CASE("ws client builds headers and dispatches rx callback", "[net][ws]")
{
    ws_client_config_t cfg = {
        .uri = "wss://sensor.local/ws",
        .auth_token = "abcdef",
        .reconnect_min_delay_ms = 500,
        .reconnect_max_delay_ms = 4000,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, capture_rx, NULL));
    TEST_ASSERT_NOT_NULL(s_last_config.headers);
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "Authorization: Bearer abcdef"));
    TEST_ASSERT_EQUAL(1, s_client_init_calls);
    TEST_ASSERT_EQUAL(1, s_client_start_calls);
    TEST_ASSERT_NOT_NULL(s_event_handler);

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_TEXT,
    };
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_CONNECTED, &evt);
    TEST_ASSERT_TRUE(ws_client_is_connected());

    uint8_t payload[8] = {0};
    ((uint32_t *)payload)[0] = 0xAABBCCDD;
    memcpy(payload + sizeof(uint32_t), "hi", 2);
    evt.op_code = WS_TRANSPORT_OPCODES_BINARY;
    evt.data_ptr = payload;
    evt.payload_len = 6;
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DATA, &evt);
    TEST_ASSERT_EQUAL(1, s_rx_calls);
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, s_last_rx_crc);
    TEST_ASSERT_EQUAL_SIZE_T(2, s_last_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hi", s_last_rx_payload, 2);

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_send((const uint8_t *)"OK", 2));
    TEST_ASSERT_EQUAL(1, s_send_calls);
}

TEST_CASE("ws client reconnect delay backs off and saturates", "[net][ws]")
{
    ws_client_config_t cfg = {
        .uri = "wss://retry",
        .reconnect_min_delay_ms = 250,
        .reconnect_max_delay_ms = 1000,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, NULL, NULL));
    TEST_ASSERT_NOT_NULL(s_event_handler);

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_TEXT,
    };

    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_EQUAL(1, s_timer_change_calls);
    TEST_ASSERT_EQUAL_UINT32(250, s_last_delay_ms);

    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_EQUAL(2, s_timer_change_calls);
    TEST_ASSERT_EQUAL_UINT32(500, s_last_delay_ms);

    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_EQUAL(3, s_timer_change_calls);
    TEST_ASSERT_EQUAL_UINT32(1000, s_last_delay_ms);

    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_EQUAL(4, s_timer_change_calls);
    TEST_ASSERT_EQUAL_UINT32(1000, s_last_delay_ms);
}

TEST_CASE("ws client send validates connection state", "[net][ws]")
{
    uint8_t payload[] = {1, 2, 3};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ws_client_send(payload, sizeof(payload)));

    ws_client_config_t cfg = {
        .uri = "wss://send",
    };
    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, capture_rx, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ws_client_send(payload, sizeof(payload)));

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_TEXT,
    };
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_CONNECTED, &evt);
    TEST_ASSERT_TRUE(ws_client_is_connected());
    TEST_ASSERT_EQUAL(ESP_OK, ws_client_send(payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_SIZE_T(sizeof(payload), s_last_send_len);
}

TEST_CASE("ws client regenerates handshake headers on reconnect", "[net][ws]")
{
    static const uint8_t secret[32] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
        0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78,
        0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0,
    };
    ws_client_config_t cfg = {
        .uri = "wss://secure",
        .auth_token = "handshake-token",
        .crypto_secret = secret,
        .crypto_secret_len = sizeof(secret),
        .enable_handshake_token = true,
        .reconnect_min_delay_ms = 100,
        .reconnect_max_delay_ms = 100,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, NULL, NULL));
    TEST_ASSERT_NOT_NULL(s_last_config.headers);
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "Authorization: Bearer handshake-token"));
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "X-WS-Nonce"));
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "X-WS-Signature"));

    char header_snapshot[256];
    strncpy(header_snapshot, s_last_config.headers, sizeof(header_snapshot));
    header_snapshot[sizeof(header_snapshot) - 1U] = '\0';

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_BINARY,
    };
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_NOT_NULL(s_timer.cb);
    s_timer.cb(&s_timer);
    TEST_ASSERT_EQUAL(2, s_client_start_calls);
    TEST_ASSERT_NOT_NULL(s_last_config.headers);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(header_snapshot, s_last_config.headers));
}

TEST_CASE("ws client emits totp header when enabled", "[net][ws]")
{
    static const uint8_t secret[] = "12345678901234567890";
    s_fake_unix_time = 59;
    ws_client_config_t cfg = {
        .uri = "wss://secure", 
        .auth_token = "totp-token",
        .crypto_secret = NULL,
        .crypto_secret_len = 0,
        .enable_handshake_token = false,
        .enable_totp = true,
        .totp_secret = secret,
        .totp_secret_len = sizeof(secret) - 1U,
        .totp_period_s = 30,
        .totp_digits = 8,
        .totp_window = 1,
        .reconnect_min_delay_ms = 100,
        .reconnect_max_delay_ms = 100,
        .get_time_unix = fake_time_provider,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, NULL, NULL));
    TEST_ASSERT_NOT_NULL(s_last_config.headers);
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "Authorization: Bearer totp-token"));
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "X-WS-TOTP: 94287082"));

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_TEXT,
    };
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_DISCONNECTED, &evt);
    TEST_ASSERT_NOT_NULL(s_timer.cb);
    s_fake_unix_time = 1111111109;
    s_timer.cb(&s_timer);
    TEST_ASSERT_NOT_NULL(s_last_config.headers);
    TEST_ASSERT_NOT_NULL(strstr(s_last_config.headers, "X-WS-TOTP: 07081804"));
}

TEST_CASE("ws client encrypts outbound payloads when enabled", "[net][ws]")
{
    static const uint8_t secret[32] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
        0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
        0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60, 0x71,
        0x82, 0x93, 0xA4, 0xB5, 0xC6, 0xD7, 0xE8, 0xF9,
    };
    ws_client_config_t cfg = {
        .uri = "wss://cipher",
        .crypto_secret = secret,
        .crypto_secret_len = sizeof(secret),
        .enable_frame_encryption = true,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, capture_rx, NULL));
    TEST_ASSERT_NULL(s_last_config.headers);

    esp_websocket_event_data_t evt = {
        .data_ptr = NULL,
        .payload_len = 0,
        .op_code = WS_TRANSPORT_OPCODES_BINARY,
    };
    s_event_handler(s_event_ctx, WEBSOCKET_EVENT_CONNECTED, &evt);
    uint8_t payload[] = {0x10, 0x20, 0x30};
    TEST_ASSERT_EQUAL(ESP_OK, ws_client_send(payload, sizeof(payload)));
    size_t expected = WS_SECURITY_HEADER_LEN + sizeof(payload) + WS_SECURITY_TAG_LEN;
    TEST_ASSERT_EQUAL_SIZE_T(expected, s_last_send_len);
}

TEST_CASE("ws client applies TLS server name override", "[net][ws]")
{
    ws_client_config_t cfg = {
        .uri = "wss://sensor",
        .tls_server_name = "sensor.example.com",
    };
    TEST_ASSERT_EQUAL(ESP_OK, ws_client_start(&cfg, NULL, NULL));
    TEST_ASSERT_NOT_NULL(s_last_config.host);
    TEST_ASSERT_EQUAL_STRING("sensor.example.com", s_last_config.host);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    TEST_ASSERT_NOT_NULL(s_last_config.common_name);
    TEST_ASSERT_EQUAL_STRING("sensor.example.com", s_last_config.common_name);
#endif
}
