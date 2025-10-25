#include "ws_server.h"

#include "unity.h"
#include <string.h>

typedef struct {
    TimerCallbackFunction_t cb;
    TickType_t period;
    bool started;
} fake_timer_t;

typedef struct {
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
    bool deleted;
} fake_semaphore_t;

static ws_server_platform_t s_platform;
static httpd_ssl_config_t s_last_ssl_cfg;
static httpd_handle_t s_fake_httpd = (httpd_handle_t)0x42;
static httpd_uri_t s_registered_ws_uri;
static httpd_uri_t s_registered_root_uri;
static httpd_ws_handler_opcode_t s_last_hook_opcode;
static httpd_ws_handler_t s_last_hook_handler;
static fake_timer_t s_timer;
static fake_semaphore_t s_semaphore;
static size_t s_send_calls;
static int s_ssl_start_calls;
static int s_httpd_stop_calls;
static int s_timer_start_calls;
static int s_timer_stop_calls;
static int s_timer_delete_calls;
static int s_semaphore_create_calls;
static int s_semaphore_delete_calls;
static int s_sess_close_calls;
static bool s_fail_next_send;
static httpd_ws_frame_t s_last_frame;
static uint8_t s_last_payload[256];
static size_t s_last_payload_len;
static int s_register_uri_calls;
static int s_register_hook_calls;
static TickType_t s_fake_tick_count;
static uint64_t s_fake_time_unix;

static uint64_t fake_time_unix(void)
{
    return s_fake_time_unix;
}

static esp_err_t fake_httpd_ssl_start(httpd_handle_t *handle, const httpd_ssl_config_t *config)
{
    ++s_ssl_start_calls;
    if (!config) {
        return ESP_FAIL;
    }
    memcpy(&s_last_ssl_cfg, config, sizeof(s_last_ssl_cfg));
    *handle = s_fake_httpd;
    return ESP_OK;
}

static esp_err_t fake_httpd_stop(httpd_handle_t handle)
{
    (void)handle;
    ++s_httpd_stop_calls;
    return ESP_OK;
}

static esp_err_t fake_httpd_register_uri(httpd_handle_t handle, const httpd_uri_t *uri)
{
    (void)handle;
    if (strcmp(uri->uri, "/ws") == 0) {
        s_registered_ws_uri = *uri;
    } else {
        s_registered_root_uri = *uri;
    }
    ++s_register_uri_calls;
    return ESP_OK;
}

static esp_err_t fake_httpd_register_hook(httpd_ws_handler_opcode_t hook, httpd_ws_handler_t handler)
{
    ++s_register_hook_calls;
    s_last_hook_opcode = hook;
    s_last_hook_handler = handler;
    return ESP_OK;
}

static esp_err_t fake_httpd_ws_send(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame)
{
    (void)handle;
    (void)fd;
    ++s_send_calls;
    memcpy(&s_last_frame, frame, sizeof(s_last_frame));
    s_last_payload_len = frame->len;
    if (frame->payload && frame->len <= sizeof(s_last_payload)) {
        memcpy(s_last_payload, frame->payload, frame->len);
    }
    if (s_fail_next_send) {
        s_fail_next_send = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t fake_httpd_ws_recv(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    (void)req;
    (void)frame;
    (void)max_len;
    return ESP_OK;
}

static int fake_httpd_req_to_sockfd(httpd_req_t *req)
{
    (void)req;
    return 7;
}

static esp_err_t fake_httpd_resp_status(httpd_req_t *req, const char *status)
{
    (void)req;
    (void)status;
    return ESP_OK;
}

static esp_err_t fake_httpd_resp_hdr(httpd_req_t *req, const char *field, const char *value)
{
    (void)req;
    (void)field;
    (void)value;
    return ESP_OK;
}

static esp_err_t fake_httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t len)
{
    (void)req;
    (void)buf;
    (void)len;
    return ESP_OK;
}

static void fake_httpd_sess_close(httpd_handle_t handle, int sockfd)
{
    (void)handle;
    (void)sockfd;
    ++s_sess_close_calls;
}

static SemaphoreHandle_t fake_semaphore_create(StaticSemaphore_t *storage)
{
    ++s_semaphore_create_calls;
    s_semaphore.storage = *storage;
    s_semaphore.handle = (SemaphoreHandle_t)&s_semaphore;
    s_semaphore.deleted = false;
    return s_semaphore.handle;
}

static BaseType_t fake_semaphore_take(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    (void)semaphore;
    (void)ticks;
    return pdPASS;
}

static BaseType_t fake_semaphore_give(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
    return pdPASS;
}

static void fake_semaphore_delete(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
    ++s_semaphore_delete_calls;
    s_semaphore.deleted = true;
}

static TimerHandle_t fake_timer_create(const char *name, TickType_t period, UBaseType_t auto_reload,
                                       void *timer_id, TimerCallbackFunction_t cb)
{
    (void)name;
    (void)auto_reload;
    (void)timer_id;
    s_timer.cb = cb;
    s_timer.period = period;
    s_timer.started = false;
    return (TimerHandle_t)&s_timer;
}

static BaseType_t fake_timer_start(TimerHandle_t timer, TickType_t ticks)
{
    (void)timer;
    (void)ticks;
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

static BaseType_t fake_timer_change(TimerHandle_t timer, TickType_t period, TickType_t ticks)
{
    (void)timer;
    (void)ticks;
    s_timer.period = period;
    return pdPASS;
}

static TickType_t fake_task_get_tick_count(void)
{
    return s_fake_tick_count++;
}

static void reset_platform(void)
{
    memset(&s_platform, 0, sizeof(s_platform));
    s_platform.httpd_ssl_start = fake_httpd_ssl_start;
    s_platform.httpd_stop = fake_httpd_stop;
    s_platform.httpd_register_uri_handler = fake_httpd_register_uri;
    s_platform.httpd_register_ws_handler_hook = fake_httpd_register_hook;
    s_platform.httpd_ws_send_frame_async = fake_httpd_ws_send;
    s_platform.httpd_ws_recv_frame = fake_httpd_ws_recv;
    s_platform.httpd_req_to_sockfd = fake_httpd_req_to_sockfd;
    s_platform.httpd_resp_set_status = fake_httpd_resp_status;
    s_platform.httpd_resp_set_hdr = fake_httpd_resp_hdr;
    s_platform.httpd_resp_send = fake_httpd_resp_send;
    s_platform.httpd_sess_trigger_close = fake_httpd_sess_close;
    s_platform.semaphore_create = fake_semaphore_create;
    s_platform.semaphore_take = fake_semaphore_take;
    s_platform.semaphore_give = fake_semaphore_give;
    s_platform.semaphore_delete = fake_semaphore_delete;
    s_platform.timer_create = fake_timer_create;
    s_platform.timer_start = fake_timer_start;
    s_platform.timer_stop = fake_timer_stop;
    s_platform.timer_delete = fake_timer_delete;
    s_platform.timer_change_period = fake_timer_change;
    s_platform.task_get_tick_count = fake_task_get_tick_count;
}

static void reset_state(void)
{
    memset(&s_last_ssl_cfg, 0, sizeof(s_last_ssl_cfg));
    memset(&s_registered_ws_uri, 0, sizeof(s_registered_ws_uri));
    memset(&s_registered_root_uri, 0, sizeof(s_registered_root_uri));
    memset(&s_timer, 0, sizeof(s_timer));
    memset(&s_semaphore, 0, sizeof(s_semaphore));
    memset(&s_last_frame, 0, sizeof(s_last_frame));
    memset(&s_last_payload, 0, sizeof(s_last_payload));
    s_send_calls = 0;
    s_ssl_start_calls = 0;
    s_httpd_stop_calls = 0;
    s_timer_start_calls = 0;
    s_timer_stop_calls = 0;
    s_timer_delete_calls = 0;
    s_semaphore_create_calls = 0;
    s_semaphore_delete_calls = 0;
    s_sess_close_calls = 0;
    s_fail_next_send = false;
    s_register_uri_calls = 0;
    s_register_hook_calls = 0;
    s_fake_tick_count = 0;
    s_last_payload_len = 0;
    s_fake_time_unix = 0;
}

void setUp(void)
{
    reset_platform();
    reset_state();
    ws_server_set_platform(&s_platform);
    ws_server_clear_clients_for_test();
}

void tearDown(void)
{
    ws_server_stop();
    ws_server_set_platform(NULL);
    ws_server_clear_clients_for_test();
}

TEST_CASE("ws server requires TLS credentials", "[net][ws]")
{
    ws_server_config_t cfg = {
        .port = 443,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ws_server_start(&cfg, NULL, NULL));
}

TEST_CASE("ws server starts, registers handlers, and broadcasts", "[net][ws]")
{
    uint8_t cert[] = {1, 2, 3};
    uint8_t key[] = {4, 5, 6};
    ws_server_config_t cfg = {
        .port = 9000,
        .server_cert = cert,
        .server_cert_len = sizeof(cert),
        .server_key = key,
        .server_key_len = sizeof(key),
        .max_clients = 2,
        .ping_interval_ms = 2000,
        .pong_timeout_ms = 1000,
        .rx_buffer_size = 128,
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_server_start(&cfg, NULL, NULL));
    TEST_ASSERT_EQUAL(1, s_ssl_start_calls);
    TEST_ASSERT_EQUAL(2, s_register_uri_calls);
    TEST_ASSERT_EQUAL(2, s_register_hook_calls);
    TEST_ASSERT_TRUE(s_timer.started);
    TEST_ASSERT_EQUAL(1, s_timer_start_calls);
    TEST_ASSERT_EQUAL(1, s_semaphore_create_calls);

    TEST_ASSERT_EQUAL(ESP_OK, ws_server_add_client_for_test(10));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ws_server_active_client_count());

    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL(ESP_OK, ws_server_send(payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)s_send_calls);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)s_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, s_last_payload, sizeof(payload));

    ws_server_stop();
    TEST_ASSERT_EQUAL(1, s_httpd_stop_calls);
    TEST_ASSERT_EQUAL(1, s_timer_delete_calls);
    TEST_ASSERT_EQUAL(1, s_semaphore_delete_calls);
}

TEST_CASE("ws server drops clients when send fails", "[net][ws]")
{
    uint8_t cert[] = {0x30};
    uint8_t key[] = {0x31};
    ws_server_config_t cfg = {
        .port = 9443,
        .server_cert = cert,
        .server_cert_len = sizeof(cert),
        .server_key = key,
        .server_key_len = sizeof(key),
    };

    TEST_ASSERT_EQUAL(ESP_OK, ws_server_start(&cfg, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, ws_server_add_client_for_test(3));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ws_server_active_client_count());

    const uint8_t payload[] = {1};
    s_fail_next_send = true;
    TEST_ASSERT_EQUAL(ESP_FAIL, ws_server_send(payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(1, s_sess_close_calls);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ws_server_active_client_count());
}

TEST_CASE("ws server validates totp configuration", "[net][ws]")
{
    uint8_t cert[] = {0x01};
    uint8_t key[] = {0x02};
    ws_server_config_t invalid = {
        .port = 9443,
        .server_cert = cert,
        .server_cert_len = sizeof(cert),
        .server_key = key,
        .server_key_len = sizeof(key),
        .enable_totp = true,
        .totp_period_s = 30,
        .totp_digits = 6,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ws_server_start(&invalid, NULL, NULL));

    uint8_t totp_secret[] = {0x10, 0x11, 0x12, 0x13};
    ws_server_config_t valid = {
        .port = 9443,
        .server_cert = cert,
        .server_cert_len = sizeof(cert),
        .server_key = key,
        .server_key_len = sizeof(key),
        .enable_totp = true,
        .totp_secret = totp_secret,
        .totp_secret_len = sizeof(totp_secret),
        .totp_period_s = 30,
        .totp_digits = 6,
        .totp_window = 1,
        .get_time_unix = fake_time_unix,
    };
    TEST_ASSERT_EQUAL(ESP_OK, ws_server_start(&valid, NULL, NULL));
    ws_server_stop();
}
