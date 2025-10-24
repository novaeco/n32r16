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

static void clients_lock(void)
{
    if (s_client_lock) {
        xSemaphoreTake(s_client_lock, portMAX_DELAY);
    }
}

static void clients_unlock(void)
{
    if (s_client_lock) {
        xSemaphoreGive(s_client_lock);
    }
}

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

static void drop_client(int fd)
{
    clients_lock();
    drop_client_locked(fd);
    clients_unlock();
}

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

static esp_err_t send_ws_frame(int fd, httpd_ws_type_t type, const uint8_t *payload, size_t len)
{
    httpd_ws_frame_t frame = {
        .type = type,
        .payload = (uint8_t *)payload,
        .len = len,
    };
    return httpd_ws_send_frame_async(s_server, fd, &frame);
}

static void ping_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_clients) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
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
            httpd_sess_trigger_close(s_server, client->fd);
            drop_client_locked(client->fd);
            continue;
        }
        if (!client->awaiting_pong && elapsed >= ping_interval) {
            if (send_ws_frame(client->fd, HTTPD_WS_TYPE_PING, NULL, 0) == ESP_OK) {
                client->awaiting_pong = true;
            } else {
                ESP_LOGW(TAG, "Ping failed, dropping client %d", client->fd);
                httpd_sess_trigger_close(s_server, client->fd);
                drop_client_locked(client->fd);
            }
        }
    }
    clients_unlock();
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (!authorize_request(req)) {
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
            httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        int fd = httpd_req_to_sockfd(req);
        if (add_client(fd) != ESP_OK) {
            httpd_resp_set_status(req, "503 Service Unavailable");
            httpd_resp_send(req, "Too many clients", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .payload = NULL,
        .len = 0,
    };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame length: %s", esp_err_to_name(ret));
        return ret;
    }
    if (frame.len > s_cfg.rx_buffer_size) {
        ESP_LOGW(TAG, "Frame too large (%zu bytes)", frame.len);
        return ESP_ERR_NO_MEM;
    }
    frame.payload = s_rx_buffer;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read frame: %s", esp_err_to_name(ret));
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);
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

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char resp[] = "ESP32 Secure WebSocket Server";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t ws_open_hook(httpd_handle_t hd, int fd)
{
    (void)hd;
    ESP_LOGI(TAG, "Client connected: %d", fd);
    return ESP_OK;
}

static void ws_close_hook(httpd_handle_t hd, int fd)
{
    (void)hd;
    ESP_LOGI(TAG, "Client disconnected: %d", fd);
    drop_client(fd);
}

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

    s_client_lock = xSemaphoreCreateMutexStatic(&s_client_lock_storage);
    if (!s_client_lock) {
        free(s_rx_buffer);
        s_rx_buffer = NULL;
        free(s_clients);
        s_clients = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ping_timer = xTimerCreate("ws_ping", pdMS_TO_TICKS(s_cfg.ping_interval_ms), pdTRUE, NULL, ping_timer_cb);
    if (!s_ping_timer) {
        vSemaphoreDelete(s_client_lock);
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

    esp_err_t ret = httpd_ssl_start(&s_server, &ssl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
        xTimerDelete(s_ping_timer, portMAX_DELAY);
        s_ping_timer = NULL;
        vSemaphoreDelete(s_client_lock);
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
    httpd_register_uri_handler(s_server, &ws_uri);

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_CONNECTED, ws_open_hook);
    httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_DISCONNECTED, ws_close_hook);

    xTimerStart(s_ping_timer, 0);
    ESP_LOGI(TAG, "WebSocket server listening on %u", s_cfg.port);
    return ESP_OK;
}

void ws_server_stop(void)
{
    if (s_ping_timer) {
        xTimerStop(s_ping_timer, portMAX_DELAY);
        xTimerDelete(s_ping_timer, portMAX_DELAY);
        s_ping_timer = NULL;
    }
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_client_lock) {
        vSemaphoreDelete(s_client_lock);
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
            httpd_sess_trigger_close(s_server, client->fd);
            drop_client_locked(client->fd);
            result = err;
        }
    }
    clients_unlock();
    return result;
}
