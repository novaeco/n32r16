#include "ws_server.h"

#include "esp_https_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int fd;
    TickType_t last_seen;
    bool awaiting_pong;
} ws_client_t;

static const char *TAG = "ws_server";

static httpd_handle_t s_server;
static ws_server_config_t s_cfg;
static ws_client_t *s_clients;
static size_t s_client_capacity;
static ws_server_rx_cb_t s_rx_cb;
static void *s_rx_ctx;
static SemaphoreHandle_t s_client_lock;
static StaticSemaphore_t s_client_lock_storage;
static TimerHandle_t s_ping_timer;
static uint8_t *s_rx_buffer;

/**
 * @brief Default hook to start the HTTPS server.
 *
 * @param handle Output pointer receiving the server handle.
 * @param config TLS server configuration.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t httpd_ssl_start_default(httpd_handle_t *handle, const httpd_ssl_config_t *config)
{
    return httpd_ssl_start(handle, config);
}

/**
 * @brief Default hook to stop the HTTPS server.
 *
 * @param handle HTTPD server handle.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t httpd_stop_default(httpd_handle_t handle)
{
    return httpd_stop(handle);
}

/**
 * @brief Default hook to register an HTTP URI handler.
 *
 * @param handle HTTPD server handle.
 * @param uri Pointer to the URI descriptor.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t httpd_register_uri_handler_default(httpd_handle_t handle, const httpd_uri_t *uri)
{
    return httpd_register_uri_handler(handle, uri);
}

/**
 * @brief Default hook to register a WebSocket lifecycle callback.
 *
 * @param hook Lifecycle event opcode.
 * @param handler Callback invoked on the event.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t httpd_register_ws_handler_hook_default(httpd_ws_handler_opcode_t hook, httpd_ws_handler_t handler)
{
    return httpd_register_ws_handler_hook(hook, handler);
}

/**
 * @brief Default hook to queue a WebSocket frame for transmission.
 *
 * @param handle HTTPD server handle.
 * @param fd Client socket descriptor.
 * @param frame Pointer to the frame descriptor.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t httpd_ws_send_frame_async_default(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame)
{
    return httpd_ws_send_frame_async(handle, fd, frame);
}

/**
 * @brief Default hook to receive a WebSocket frame.
 *
 * @param req HTTP request context.
 * @param frame Frame descriptor to populate.
 * @param max_len Maximum payload length to read.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t httpd_ws_recv_frame_default(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    return httpd_ws_recv_frame(req, frame, max_len);
}

/**
 * @brief Default hook to obtain the socket descriptor from a request.
 *
 * @param req HTTP request context.
 * @return Socket descriptor associated with the request.
 */
static int httpd_req_to_sockfd_default(httpd_req_t *req)
{
    return httpd_req_to_sockfd(req);
}

/**
 * @brief Default hook to set the HTTP response status line.
 *
 * @param req HTTP request context.
 * @param status Status string (e.g., "200 OK").
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t httpd_resp_set_status_default(httpd_req_t *req, const char *status)
{
    return httpd_resp_set_status(req, status);
}

/**
 * @brief Default hook to append a header to the HTTP response.
 *
 * @param req HTTP request context.
 * @param field Header field name.
 * @param value Header value string.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t httpd_resp_set_hdr_default(httpd_req_t *req, const char *field, const char *value)
{
    return httpd_resp_set_hdr(req, field, value);
}

/**
 * @brief Default hook to send an HTTP response body.
 *
 * @param req HTTP request context.
 * @param buf Pointer to the response buffer.
 * @param buf_len Length of the response buffer or HTTPD_RESP_USE_STRLEN.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t httpd_resp_send_default(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    return httpd_resp_send(req, buf, buf_len);
}

/**
 * @brief Default hook to close an HTTPD session socket.
 *
 * @param handle HTTPD server handle.
 * @param sockfd Client socket descriptor.
 * @return void
 */
static void httpd_sess_trigger_close_default(httpd_handle_t handle, int sockfd)
{
    httpd_sess_trigger_close(handle, sockfd);
}

/**
 * @brief Default hook to create a mutex semaphore using static storage.
 *
 * @param storage Pointer to the static semaphore storage buffer.
 * @return Handle to the created semaphore.
 */
static SemaphoreHandle_t semaphore_create_default(StaticSemaphore_t *storage)
{
    return xSemaphoreCreateMutexStatic(storage);
}

/**
 * @brief Default hook to acquire a semaphore.
 *
 * @param semaphore Semaphore handle.
 * @param ticks Maximum wait time in ticks.
 * @return pdTRUE on success or pdFALSE on timeout.
 */
static BaseType_t semaphore_take_default(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    return xSemaphoreTake(semaphore, ticks);
}

/**
 * @brief Default hook to release a semaphore.
 *
 * @param semaphore Semaphore handle.
 * @return pdTRUE on success or pdFALSE on failure.
 */
static BaseType_t semaphore_give_default(SemaphoreHandle_t semaphore)
{
    return xSemaphoreGive(semaphore);
}

/**
 * @brief Default hook to delete a semaphore.
 *
 * @param semaphore Semaphore handle.
 * @return void
 */
static void semaphore_delete_default(SemaphoreHandle_t semaphore)
{
    vSemaphoreDelete(semaphore);
}

/**
 * @brief Default hook to create a FreeRTOS timer.
 *
 * @param name Timer name string.
 * @param period Timer period in ticks.
 * @param auto_reload Auto-reload flag.
 * @param timer_id User context pointer.
 * @param callback Function invoked on expiration.
 * @return Handle to the created timer or NULL on failure.
 */
static TimerHandle_t timer_create_default(const char *name, TickType_t period, UBaseType_t auto_reload,
                                         void *timer_id, TimerCallbackFunction_t callback)
{
    return xTimerCreate(name, period, auto_reload, timer_id, callback);
}

/**
 * @brief Default hook to start a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks Maximum wait time in ticks.
 * @return pdPASS on success or pdFAIL on failure.
 */
static BaseType_t timer_start_default(TimerHandle_t timer, TickType_t ticks)
{
    return xTimerStart(timer, ticks);
}

/**
 * @brief Default hook to stop a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks Maximum wait time in ticks.
 * @return pdPASS on success or pdFAIL on failure.
 */
static BaseType_t timer_stop_default(TimerHandle_t timer, TickType_t ticks)
{
    return xTimerStop(timer, ticks);
}

/**
 * @brief Default hook to delete a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks Maximum wait time in ticks.
 * @return pdPASS on success or pdFAIL on failure.
 */
static BaseType_t timer_delete_default(TimerHandle_t timer, TickType_t ticks)
{
    return xTimerDelete(timer, ticks);
}

/**
 * @brief Default hook to update a FreeRTOS timer period.
 *
 * @param timer Timer handle.
 * @param period New period in ticks.
 * @param ticks Maximum wait time in ticks.
 * @return pdPASS on success or pdFAIL on failure.
 */
static BaseType_t timer_change_period_default(TimerHandle_t timer, TickType_t period, TickType_t ticks)
{
    return xTimerChangePeriod(timer, period, ticks);
}

/**
 * @brief Default hook to read the current RTOS tick count.
 *
 * @return Current tick count.
 */
static TickType_t task_get_tick_count_default(void)
{
    return xTaskGetTickCount();
}

static const ws_server_platform_t s_default_platform = {
    .httpd_ssl_start = httpd_ssl_start_default,
    .httpd_stop = httpd_stop_default,
    .httpd_register_uri_handler = httpd_register_uri_handler_default,
    .httpd_register_ws_handler_hook = httpd_register_ws_handler_hook_default,
    .httpd_ws_send_frame_async = httpd_ws_send_frame_async_default,
    .httpd_ws_recv_frame = httpd_ws_recv_frame_default,
    .httpd_req_to_sockfd = httpd_req_to_sockfd_default,
    .httpd_resp_set_status = httpd_resp_set_status_default,
    .httpd_resp_set_hdr = httpd_resp_set_hdr_default,
    .httpd_resp_send = httpd_resp_send_default,
    .httpd_sess_trigger_close = httpd_sess_trigger_close_default,
    .semaphore_create = semaphore_create_default,
    .semaphore_take = semaphore_take_default,
    .semaphore_give = semaphore_give_default,
    .semaphore_delete = semaphore_delete_default,
    .timer_create = timer_create_default,
    .timer_start = timer_start_default,
    .timer_stop = timer_stop_default,
    .timer_delete = timer_delete_default,
    .timer_change_period = timer_change_period_default,
    .task_get_tick_count = task_get_tick_count_default,
};

static const ws_server_platform_t *s_platform = &s_default_platform;

/**
 * @brief Acquire the client table lock if available.
 *
 * @return void
 */
static void clients_lock(void)
{
    if (s_client_lock) {
        s_platform->semaphore_take(s_client_lock, portMAX_DELAY);
    }
}

/**
 * @brief Release the client table lock if held.
 *
 * @return void
 */
static void clients_unlock(void)
{
    if (s_client_lock) {
        s_platform->semaphore_give(s_client_lock);
    }
}

/**
 * @brief Find a client entry by socket descriptor.
 *
 * @param fd Client socket descriptor.
 * @return Pointer to the client entry or NULL when not found.
 */
static ws_client_t *find_client(int fd)
{
    if (!s_clients) {
        return NULL;
    }
    for (size_t i = 0; i < s_client_capacity; ++i) {
        if (s_clients[i].fd == fd) {
            return &s_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief Remove a client from the active table (lock must be held).
 *
 * @param fd Client socket descriptor to remove.
 * @return void
 */
static void drop_client_locked(int fd)
{
    if (!s_clients) {
        return;
    }
    for (size_t i = 0; i < s_client_capacity; ++i) {
        if (s_clients[i].fd == fd) {
            ESP_LOGI(TAG, "Client removed: %d", fd);
            s_clients[i].fd = -1;
            s_clients[i].last_seen = 0;
            s_clients[i].awaiting_pong = false;
            break;
        }
    }
}

/**
 * @brief Remove a client from the active table with internal locking.
 *
 * @param fd Client socket descriptor to remove.
 * @return void
 */
static void drop_client(int fd)
{
    clients_lock();
    drop_client_locked(fd);
    clients_unlock();
}

/**
 * @brief Add a client socket to the active table.
 *
 * @param fd Client socket descriptor to register.
 * @return ESP_OK on success or ESP_FAIL when capacity is exhausted.
 */
static esp_err_t add_client(int fd)
{
    esp_err_t err = ESP_FAIL;
    clients_lock();
    if (!s_clients) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        for (size_t i = 0; i < s_client_capacity; ++i) {
            if (s_clients[i].fd < 0) {
                s_clients[i].fd = fd;
                s_clients[i].last_seen = xTaskGetTickCount();
                s_clients[i].awaiting_pong = false;
                ESP_LOGI(TAG, "Client registered: %d", fd);
                err = ESP_OK;
                break;
            }
        }
    }
    clients_unlock();
    return err;
}

/**
 * @brief Validate the Authorization header against the configured token.
 *
 * @param req HTTP request context.
 * @return true when the request is authorized, false otherwise.
 */
static bool authorize_request(httpd_req_t *req)
{
    if (!s_cfg.auth_token || strlen(s_cfg.auth_token) == 0) {
        return true;
    }
    char header[128] = {0};
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header));
    if (err != ESP_OK) {
        return false;
    }
    const char *prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);
    if (strncmp(header, prefix, prefix_len) != 0) {
        return false;
    }
    return strcmp(header + prefix_len, s_cfg.auth_token) == 0;
}

/**
 * @brief Send a WebSocket frame to a client using the platform abstraction.
 *
 * @param fd Client socket descriptor.
 * @param type WebSocket frame type.
 * @param payload Pointer to the payload buffer.
 * @param len Payload length in bytes.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t send_ws_frame(int fd, httpd_ws_type_t type, const uint8_t *payload, size_t len)
{
    httpd_ws_frame_t frame = {
        .type = type,
        .payload = (uint8_t *)payload,
        .len = len,
    };
    return s_platform->httpd_ws_send_frame_async(s_server, fd, &frame);
}

/**
 * @brief Periodic timer that dispatches ping frames and handles timeouts.
 *
 * @param timer Timer handle invoking the callback.
 * @return void
 */
static void ping_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_clients) {
        return;
    }
    const TickType_t now = s_platform->task_get_tick_count();
    const TickType_t ping_interval = pdMS_TO_TICKS(s_cfg.ping_interval_ms);
    const TickType_t pong_timeout = pdMS_TO_TICKS(s_cfg.pong_timeout_ms);
    clients_lock();
    for (size_t i = 0; i < s_client_capacity; ++i) {
        ws_client_t *client = &s_clients[i];
        if (client->fd < 0) {
            continue;
        }
        TickType_t elapsed = now - client->last_seen;
        if (client->awaiting_pong && elapsed >= pong_timeout) {
            ESP_LOGW(TAG, "Client timeout: %d", client->fd);
            s_platform->httpd_sess_trigger_close(s_server, client->fd);
            drop_client_locked(client->fd);
            continue;
        }
        if (!client->awaiting_pong && elapsed >= ping_interval) {
            if (send_ws_frame(client->fd, HTTPD_WS_TYPE_PING, NULL, 0) == ESP_OK) {
                client->awaiting_pong = true;
            } else {
                ESP_LOGW(TAG, "Ping failed, dropping client %d", client->fd);
                s_platform->httpd_sess_trigger_close(s_server, client->fd);
                drop_client_locked(client->fd);
            }
        }
    }
    clients_unlock();
}

/**
 * @brief Primary WebSocket endpoint handler for HTTPD.
 *
 * @param req HTTP request context.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (!authorize_request(req)) {
            s_platform->httpd_resp_set_status(req, "401 Unauthorized");
            s_platform->httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
            s_platform->httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        int fd = s_platform->httpd_req_to_sockfd(req);
        if (add_client(fd) != ESP_OK) {
            s_platform->httpd_resp_set_status(req, "503 Service Unavailable");
            s_platform->httpd_resp_send(req, "Too many clients", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .payload = NULL,
        .len = 0,
    };
    esp_err_t ret = s_platform->httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame length: %s", esp_err_to_name(ret));
        return ret;
    }
    if (frame.len > s_cfg.rx_buffer_size) {
        ESP_LOGW(TAG, "Frame too large (%zu bytes)", frame.len);
        return ESP_ERR_NO_MEM;
    }
    frame.payload = s_rx_buffer;
    ret = s_platform->httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read frame: %s", esp_err_to_name(ret));
        return ret;
    }

    int fd = s_platform->httpd_req_to_sockfd(req);
    ws_client_t *client = find_client(fd);
    if (!client) {
        ESP_LOGW(TAG, "Frame from unknown client %d", fd);
        return ESP_ERR_INVALID_STATE;
    }
    client->last_seen = xTaskGetTickCount();

    if (frame.type == HTTPD_WS_TYPE_PONG) {
        client->awaiting_pong = false;
        return ESP_OK;
    }
    if (frame.type == HTTPD_WS_TYPE_PING) {
        send_ws_frame(fd, HTTPD_WS_TYPE_PONG, frame.payload, frame.len);
        return ESP_OK;
    }

    if (s_rx_cb) {
        uint32_t crc32 = 0;
        const uint8_t *payload = frame.payload;
        size_t len = frame.len;
        if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len >= sizeof(uint32_t)) {
            crc32 = ((const uint32_t *)payload)[0];
            payload += sizeof(uint32_t);
            len -= sizeof(uint32_t);
        }
        s_rx_cb(payload, len, crc32, s_rx_ctx);
    }
    client->awaiting_pong = false;
    return ESP_OK;
}

/**
 * @brief Serve the HTTP root endpoint with a simple banner.
 *
 * @param req HTTP request context.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char resp[] = "ESP32 Secure WebSocket Server";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief WebSocket hook called when a client completes the handshake.
 *
 * @param hd HTTPD server handle (unused).
 * @param fd Client socket descriptor.
 * @return ESP_OK after acknowledging the connection.
 */
static esp_err_t ws_open_hook(httpd_handle_t hd, int fd)
{
    (void)hd;
    ESP_LOGI(TAG, "Client connected: %d", fd);
    return ESP_OK;
}

/**
 * @brief WebSocket hook invoked when a client disconnects.
 *
 * @param hd HTTPD server handle (unused).
 * @param fd Client socket descriptor.
 * @return void
 */
static void ws_close_hook(httpd_handle_t hd, int fd)
{
    (void)hd;
    ESP_LOGI(TAG, "Client disconnected: %d", fd);
    drop_client(fd);
}

/**
 * @brief Start the secure WebSocket server with the provided configuration.
 *
 * @param config Pointer to the server configuration structure.
 * @param cb Receive callback for inbound application frames.
 * @param ctx User context passed to the receive callback.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t ws_server_start(const ws_server_config_t *config, ws_server_rx_cb_t cb, void *ctx)
{
    if (!config || !config->server_cert || !config->server_key || config->server_cert_len == 0 ||
        config->server_key_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg = *config;
    if (s_cfg.max_clients == 0) {
        s_cfg.max_clients = 4;
    }
    if (s_cfg.rx_buffer_size == 0) {
        s_cfg.rx_buffer_size = 2048;
    }
    if (s_cfg.ping_interval_ms == 0) {
        s_cfg.ping_interval_ms = 10000;
    }
    if (s_cfg.pong_timeout_ms == 0) {
        s_cfg.pong_timeout_ms = 5000;
    }

    s_client_capacity = s_cfg.max_clients;
    s_clients = calloc(s_client_capacity, sizeof(ws_client_t));
    if (!s_clients) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < s_client_capacity; ++i) {
        s_clients[i].fd = -1;
    }

    s_rx_buffer = malloc(s_cfg.rx_buffer_size + 1);
    if (!s_rx_buffer) {
        free(s_clients);
        s_clients = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_client_lock = s_platform->semaphore_create(&s_client_lock_storage);
    if (!s_client_lock) {
        free(s_rx_buffer);
        s_rx_buffer = NULL;
        free(s_clients);
        s_clients = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ping_timer = s_platform->timer_create("ws_ping", pdMS_TO_TICKS(s_cfg.ping_interval_ms), pdTRUE, NULL,
                                            ping_timer_cb);
    if (!s_ping_timer) {
        s_platform->semaphore_delete(s_client_lock);
        s_client_lock = NULL;
        free(s_rx_buffer);
        s_rx_buffer = NULL;
        free(s_clients);
        s_clients = NULL;
        return ESP_ERR_NO_MEM;
    }

    httpd_ssl_config_t ssl_cfg = HTTPD_SSL_CONFIG_DEFAULT();
    ssl_cfg.httpd = HTTPD_DEFAULT_CONFIG();
    ssl_cfg.httpd.server_port = s_cfg.port;
    ssl_cfg.httpd.ctrl_port = s_cfg.port + 1;
    ssl_cfg.httpd.core_id = 1;
    ssl_cfg.httpd.max_open_sockets = s_cfg.max_clients + 4;
    ssl_cfg.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    ssl_cfg.servercert = s_cfg.server_cert;
    ssl_cfg.servercert_len = s_cfg.server_cert_len;
    ssl_cfg.prvtkey = s_cfg.server_key;
    ssl_cfg.prvtkey_len = s_cfg.server_key_len;

    s_rx_cb = cb;
    s_rx_ctx = ctx;

    esp_err_t ret = s_platform->httpd_ssl_start(&s_server, &ssl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
        s_platform->timer_delete(s_ping_timer, portMAX_DELAY);
        s_ping_timer = NULL;
        s_platform->semaphore_delete(s_client_lock);
        s_client_lock = NULL;
        free(s_rx_buffer);
        s_rx_buffer = NULL;
        free(s_clients);
        s_clients = NULL;
        return ret;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    s_platform->httpd_register_uri_handler(s_server, &ws_uri);

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    s_platform->httpd_register_uri_handler(s_server, &root_uri);

    s_platform->httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_CONNECTED, ws_open_hook);
    s_platform->httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_DISCONNECTED, ws_close_hook);

    s_platform->timer_start(s_ping_timer, 0);
    ESP_LOGI(TAG, "WebSocket server listening on %u", s_cfg.port);
    return ESP_OK;
}

/**
 * @brief Stop the WebSocket server and free allocated resources.
 *
 * @return void
 */
void ws_server_stop(void)
{
    if (s_ping_timer) {
        s_platform->timer_stop(s_ping_timer, portMAX_DELAY);
        s_platform->timer_delete(s_ping_timer, portMAX_DELAY);
        s_ping_timer = NULL;
    }
    if (s_server) {
        s_platform->httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_client_lock) {
        s_platform->semaphore_delete(s_client_lock);
        s_client_lock = NULL;
    }
    free(s_rx_buffer);
    s_rx_buffer = NULL;
    free(s_clients);
    s_clients = NULL;
    s_client_capacity = 0;
    s_rx_cb = NULL;
    s_rx_ctx = NULL;
}

/**
 * @brief Broadcast a binary payload to all connected WebSocket clients.
 *
 * @param data Pointer to the payload buffer.
 * @param len Payload length in bytes.
 * @return ESP_OK when all frames are queued, otherwise the last error seen.
 */
esp_err_t ws_server_send(const uint8_t *data, size_t len)
{
    if (!s_server || !data || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    clients_lock();
    for (size_t i = 0; i < s_client_capacity; ++i) {
        ws_client_t *client = &s_clients[i];
        if (client->fd < 0) {
            continue;
        }
        esp_err_t err = send_ws_frame(client->fd, HTTPD_WS_TYPE_BINARY, data, len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Send failed to %d: %s", client->fd, esp_err_to_name(err));
            s_platform->httpd_sess_trigger_close(s_server, client->fd);
            drop_client_locked(client->fd);
            result = err;
        }
    }
    clients_unlock();
    return result;
}

/**
 * @brief Return the number of currently connected WebSocket clients.
 *
 * @return Active client count.
 */
size_t ws_server_active_client_count(void)
{
    size_t count = 0;
    clients_lock();
    if (s_clients) {
        for (size_t i = 0; i < s_client_capacity; ++i) {
            if (s_clients[i].fd >= 0) {
                ++count;
            }
        }
    }
    clients_unlock();
    return count;
}

/**
 * @brief Inject a fake client entry for unit testing.
 *
 * @param fd Socket descriptor representing the client.
 * @return ESP_OK on success or an error code when capacity is exceeded.
 */
esp_err_t ws_server_add_client_for_test(int fd)
{
    return add_client(fd);
}

/**
 * @brief Remove all clients from the table for test teardown.
 *
 * @return void
 */
void ws_server_clear_clients_for_test(void)
{
    clients_lock();
    if (s_clients) {
        for (size_t i = 0; i < s_client_capacity; ++i) {
            s_clients[i].fd = -1;
            s_clients[i].last_seen = 0;
            s_clients[i].awaiting_pong = false;
        }
    }
    clients_unlock();
}

/**
 * @brief Override the WebSocket server platform hooks.
 *
 * @param platform Pointer to the platform definition (NULL for defaults).
 * @return void
 */
void ws_server_set_platform(const ws_server_platform_t *platform)
{
    s_platform = platform ? platform : &s_default_platform;
}
