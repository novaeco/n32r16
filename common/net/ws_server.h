#pragma once

#include "esp_err.h"
#include "esp_https_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t port;
    size_t max_clients;
    const char *auth_token;
    const uint8_t *server_cert;
    size_t server_cert_len;
    const uint8_t *server_key;
    size_t server_key_len;
    uint32_t ping_interval_ms;
    uint32_t pong_timeout_ms;
    size_t rx_buffer_size;
    const uint8_t *crypto_secret;
    size_t crypto_secret_len;
    bool enable_frame_encryption;
    bool enable_handshake_token;
    uint32_t handshake_replay_window_ms;
    size_t handshake_cache_size;
} ws_server_config_t;

typedef void (*ws_server_rx_cb_t)(const uint8_t *data, size_t len, uint32_t crc32, void *ctx);

typedef struct {
    esp_err_t (*httpd_ssl_start)(httpd_handle_t *handle, const httpd_ssl_config_t *config);
    esp_err_t (*httpd_stop)(httpd_handle_t handle);
    esp_err_t (*httpd_register_uri_handler)(httpd_handle_t handle, const httpd_uri_t *uri);
    esp_err_t (*httpd_register_ws_handler_hook)(httpd_ws_handler_opcode_t hook, httpd_ws_handler_t handler);
    esp_err_t (*httpd_ws_send_frame_async)(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame);
    esp_err_t (*httpd_ws_recv_frame)(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len);
    int (*httpd_req_to_sockfd)(httpd_req_t *req);
    esp_err_t (*httpd_resp_set_status)(httpd_req_t *req, const char *status);
    esp_err_t (*httpd_resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);
    esp_err_t (*httpd_resp_send)(httpd_req_t *req, const char *buf, ssize_t buf_len);
    void (*httpd_sess_trigger_close)(httpd_handle_t handle, int sockfd);
    SemaphoreHandle_t (*semaphore_create)(StaticSemaphore_t *storage);
    BaseType_t (*semaphore_take)(SemaphoreHandle_t semaphore, TickType_t ticks);
    BaseType_t (*semaphore_give)(SemaphoreHandle_t semaphore);
    void (*semaphore_delete)(SemaphoreHandle_t semaphore);
    TimerHandle_t (*timer_create)(const char *name, TickType_t period, UBaseType_t auto_reload, void *timer_id,
                                  TimerCallbackFunction_t callback);
    BaseType_t (*timer_start)(TimerHandle_t timer, TickType_t ticks);
    BaseType_t (*timer_stop)(TimerHandle_t timer, TickType_t ticks);
    BaseType_t (*timer_delete)(TimerHandle_t timer, TickType_t ticks);
    BaseType_t (*timer_change_period)(TimerHandle_t timer, TickType_t period, TickType_t ticks);
    TickType_t (*task_get_tick_count)(void);
} ws_server_platform_t;

esp_err_t ws_server_start(const ws_server_config_t *config, ws_server_rx_cb_t cb, void *ctx);
void ws_server_stop(void);
esp_err_t ws_server_send(const uint8_t *data, size_t len);
size_t ws_server_active_client_count(void);
esp_err_t ws_server_add_client_for_test(int fd);
void ws_server_clear_clients_for_test(void);
void ws_server_set_platform(const ws_server_platform_t *platform);
