#include "ws_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "ws_server";

static httpd_handle_t s_server;
static int s_client_fd = -1;
static ws_server_rx_cb_t s_rx_cb;
static void *s_rx_ctx;

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        s_client_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .payload = NULL,
        .len = 0,
    };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive frame length: %s", esp_err_to_name(ret));
        return ret;
    }
    frame.payload = malloc(frame.len + 1);
    if (!frame.payload) {
        return ESP_ERR_NO_MEM;
    }
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive frame data: %s", esp_err_to_name(ret));
        free(frame.payload);
        return ret;
    }
    frame.payload[frame.len] = '\0';

    if (s_rx_cb) {
        uint32_t crc32 = 0;
        if (frame.len >= sizeof(uint32_t) && frame.type == HTTPD_WS_TYPE_BINARY) {
            crc32 = ((uint32_t *)frame.payload)[0];
            s_rx_cb(frame.payload + sizeof(uint32_t), frame.len - sizeof(uint32_t), crc32, s_rx_ctx);
        } else {
            s_rx_cb(frame.payload, frame.len, 0, s_rx_ctx);
        }
    }
    free(frame.payload);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char resp[] = "ESP32 WebSocket Server";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ws_server_start(uint16_t port, ws_server_rx_cb_t cb, void *ctx)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = port + 1;
    config.core_id = 1;

    s_rx_cb = cb;
    s_rx_ctx = ctx;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
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
    return ESP_OK;
}

void ws_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        s_client_fd = -1;
    }
}

esp_err_t ws_server_send(const uint8_t *data, size_t len)
{
    if (!s_server || s_client_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_ws_frame_t frame = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = HTTPD_WS_TYPE_BINARY,
    };
    return httpd_ws_send_frame_async(s_server, s_client_fd, &frame);
}

static esp_err_t ws_open_handler(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    s_client_fd = sockfd;
    ESP_LOGI(TAG, "Client connected: %d", sockfd);
    return ESP_OK;
}

static void ws_close_handler(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    if (sockfd == s_client_fd) {
        ESP_LOGI(TAG, "Client disconnected: %d", sockfd);
        s_client_fd = -1;
    }
}

static void __attribute__((constructor)) register_ws_hooks(void)
{
    httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_CONNECTED, ws_open_handler);
    httpd_register_ws_handler_hook(HTTPD_WS_CLIENT_DISCONNECTED, ws_close_handler);
}
