#include "ws_server_core.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ws_server";

static httpd_handle_t s_server;
static ws_server_rx_cb_t s_rx_cb;
static ws_server_client_event_cb_t s_client_cb;
static SemaphoreHandle_t s_client_mutex;
static int s_clients[WS_SERVER_MAX_CLIENTS];
static size_t s_client_count;

static void reset_clients(void) {
    if (s_client_mutex != NULL) {
        xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    }
    for (size_t i = 0; i < WS_SERVER_MAX_CLIENTS; ++i) {
        s_clients[i] = -1;
    }
    s_client_count = 0;
    if (s_client_mutex != NULL) {
        xSemaphoreGive(s_client_mutex);
    }
}

static void notify_client_event(int fd, ws_server_client_event_t event) {
    if (s_client_cb != NULL) {
        s_client_cb(fd, event);
    }
}

static bool add_client_locked(int fd) {
    for (size_t i = 0; i < WS_SERVER_MAX_CLIENTS; ++i) {
        if (s_clients[i] == fd) {
            return true;
        }
    }
    for (size_t i = 0; i < WS_SERVER_MAX_CLIENTS; ++i) {
        if (s_clients[i] < 0) {
            s_clients[i] = fd;
            s_client_count++;
            return true;
        }
    }
    ESP_LOGW(TAG, "Max WS clients reached; rejecting fd=%d", fd);
    return false;
}

static void remove_client_locked(int fd) {
    for (size_t i = 0; i < WS_SERVER_MAX_CLIENTS; ++i) {
        if (s_clients[i] == fd) {
            s_clients[i] = -1;
            if (s_client_count > 0) {
                s_client_count--;
            }
            break;
        }
    }
}

static size_t copy_clients(int *out_list, size_t max) {
    size_t copied = 0;
    if (s_client_mutex == NULL || out_list == NULL || max == 0) {
        return 0;
    }
    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;
    }
    for (size_t i = 0; i < WS_SERVER_MAX_CLIENTS && copied < max; ++i) {
        if (s_clients[i] >= 0) {
            out_list[copied++] = s_clients[i];
        }
    }
    xSemaphoreGive(s_client_mutex);
    return copied;
}

static void purge_client(int fd) {
    if (s_client_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        remove_client_locked(fd);
        xSemaphoreGive(s_client_mutex);
    }
    notify_client_event(fd, WS_SERVER_CLIENT_DISCONNECTED);
}

static esp_err_t websocket_handler(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        if (s_client_mutex != NULL &&
            xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (add_client_locked(fd)) {
                notify_client_event(fd, WS_SERVER_CLIENT_CONNECTED);
            }
            xSemaphoreGive(s_client_mutex);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws recv frame len error: %s", esp_err_to_name(ret));
        purge_client(fd);
        return ret;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        purge_client(fd);
        return ESP_OK;
    }
    if (frame.len == 0) {
        return ESP_OK;
    }
    frame.payload = malloc(frame.len);
    if (frame.payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret == ESP_OK && s_rx_cb != NULL) {
        s_rx_cb(frame.payload, frame.len);
    }
    free(frame.payload);
    if (ret != ESP_OK) {
        purge_client(fd);
    }
    return ret;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = websocket_handler,
    .is_websocket = true,
};

esp_err_t ws_server_start(const ws_server_config_t *cfg) {
    if (cfg == NULL || cfg->server_cert == NULL || cfg->private_key == NULL ||
        cfg->server_cert_len == 0 || cfg->private_key_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rx_cb = cfg->on_rx;
    s_client_cb = cfg->on_client_event;

    s_client_mutex = xSemaphoreCreateMutex();
    if (s_client_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    reset_clients();

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.server_port = cfg->port;
    config.httpd.core_id = 0;
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    config.httpd.lru_purge_enable = true;
    config.servercert = cfg->server_cert;
    config.servercert_len = cfg->server_cert_len;
    config.prvtkey = cfg->private_key;
    config.prvtkey_len = cfg->private_key_len;

    esp_err_t err = httpd_ssl_start(&s_server, &config);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &ws_uri);
    if (err != ESP_OK) {
        httpd_ssl_stop(s_server);
        s_server = NULL;
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
    }
    return err;
}

static esp_err_t send_frame_to_fd(int fd, const uint8_t *data, size_t len) {
    if (s_server == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = HTTPD_WS_TYPE_TEXT,
    };

    esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send frame to fd=%d: %s", fd, esp_err_to_name(err));
        purge_client(fd);
    }
    return err;
}

esp_err_t ws_server_send(const uint8_t *data, size_t len) {
    int clients[WS_SERVER_MAX_CLIENTS];
    size_t count = copy_clients(clients, WS_SERVER_MAX_CLIENTS);
    if (count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = send_frame_to_fd(clients[i], data, len);
        if (err != ESP_OK) {
            result = err;
        }
    }
    return result;
}

esp_err_t ws_server_send_to(int fd, const uint8_t *data, size_t len) {
    return send_frame_to_fd(fd, data, len);
}

size_t ws_server_active_client_count(void) {
    size_t count = 0;
    if (s_client_mutex != NULL &&
        xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = s_client_count;
        xSemaphoreGive(s_client_mutex);
    }
    return count;
}

void ws_server_stop(void) {
    if (s_server != NULL) {
        httpd_ssl_stop(s_server);
        s_server = NULL;
    }
    if (s_client_mutex != NULL) {
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
    }
    reset_clients();
    s_rx_cb = NULL;
    s_client_cb = NULL;
}

