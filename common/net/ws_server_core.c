#include "ws_server_core.h"

#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "ws_server";
static httpd_handle_t s_server;
static int s_fd = -1;
static ws_server_rx_cb_t s_rx_cb;

static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        s_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws recv frame len error: %s", esp_err_to_name(ret));
        return ret;
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
    return ret;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = websocket_handler,
    .is_websocket = true,
};

esp_err_t ws_server_start(const ws_server_config_t *cfg) {
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rx_cb = cfg->on_rx;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = cfg->port;
    config.core_id = 0;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    return httpd_register_uri_handler(s_server, &ws_uri);
}

esp_err_t ws_server_send(const uint8_t *data, size_t len) {
    if (s_server == NULL || data == NULL || len == 0 || s_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = HTTPD_WS_TYPE_TEXT,
    };

    return httpd_ws_send_frame_async(s_server, s_fd, &frame);
}

void ws_server_stop(void) {
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    s_fd = -1;
}

