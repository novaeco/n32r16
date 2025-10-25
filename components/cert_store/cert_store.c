#include "cert_store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sdkconfig.h"

extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");
extern const uint8_t server_key_pem_start[] asm("_binary_server_key_pem_start");
extern const uint8_t server_key_pem_end[] asm("_binary_server_key_pem_end");
extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t ca_cert_pem_end[] asm("_binary_ca_cert_pem_end");

typedef struct {
    const uint8_t *embedded;
    size_t embedded_len;
    uint8_t *override;
    size_t override_len;
} cert_store_blob_t;

static const char *TAG = "cert_store";

static cert_store_blob_t s_server_cert = {
    .embedded = server_cert_pem_start,
    .embedded_len = (size_t)(server_cert_pem_end - server_cert_pem_start),
};

static cert_store_blob_t s_server_key = {
    .embedded = server_key_pem_start,
    .embedded_len = (size_t)(server_key_pem_end - server_key_pem_start),
};

static cert_store_blob_t s_ca_cert = {
    .embedded = ca_cert_pem_start,
    .embedded_len = (size_t)(ca_cert_pem_end - ca_cert_pem_start),
};

static bool s_initialized;
static bool s_spiffs_mounted;

static void cert_store_load_from_nvs(const char *key, cert_store_blob_t *blob)
{
#if CONFIG_CERT_STORE_OVERRIDE_FROM_NVS
    if (blob->override) {
        return;
    }

    esp_err_t err = nvs_flash_init_partition(CONFIG_CERT_STORE_NVS_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition '%s' requires maintenance: %s", CONFIG_CERT_STORE_NVS_PARTITION, esp_err_to_name(err));
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed for partition '%s': %s", CONFIG_CERT_STORE_NVS_PARTITION, esp_err_to_name(err));
        return;
    }

    nvs_handle_t handle;
    err = nvs_open_from_partition(CONFIG_CERT_STORE_NVS_PARTITION, CONFIG_CERT_STORE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS open failed for namespace '%s': %s", CONFIG_CERT_STORE_NVS_NAMESPACE, esp_err_to_name(err));
        }
        return;
    }

    size_t required = 0;
    err = nvs_get_blob(handle, key, NULL, &required);
    if (err == ESP_OK && required > 0) {
        uint8_t *buffer = (uint8_t *)malloc(required + 1);
        if (!buffer) {
            ESP_LOGE(TAG, "Out of memory loading '%s' from NVS", key);
            nvs_close(handle);
            return;
        }
        err = nvs_get_blob(handle, key, buffer, &required);
        if (err == ESP_OK) {
            buffer[required] = '\0';
            blob->override = buffer;
            blob->override_len = required;
            ESP_LOGI(TAG, "Loaded %zu B override for '%s' from NVS", required, key);
        } else {
            ESP_LOGW(TAG, "Failed to read blob '%s' from NVS: %s", key, esp_err_to_name(err));
            free(buffer);
        }
    }
    nvs_close(handle);
#else
    (void)key;
    (void)blob;
#endif
}

static esp_err_t cert_store_mount_spiffs(void)
{
#if CONFIG_CERT_STORE_OVERRIDE_FROM_SPIFFS
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_CERT_STORE_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_CERT_STORE_SPIFFS_PARTITION_LABEL,
        .max_files = 6,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK) {
        s_spiffs_mounted = true;
        ESP_LOGI(TAG, "Mounted SPIFFS partition '%s' at '%s'", CONFIG_CERT_STORE_SPIFFS_PARTITION_LABEL, CONFIG_CERT_STORE_SPIFFS_MOUNT_POINT);
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to mount SPIFFS '%s': %s", CONFIG_CERT_STORE_SPIFFS_PARTITION_LABEL, esp_err_to_name(err));
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void cert_store_load_from_spiffs(const char *filename, cert_store_blob_t *blob)
{
#if CONFIG_CERT_STORE_OVERRIDE_FROM_SPIFFS
    if (blob->override) {
        return;
    }
    if (cert_store_mount_spiffs() != ESP_OK) {
        return;
    }

    char path[128];
    int written = snprintf(path, sizeof(path), "%s/%s", CONFIG_CERT_STORE_SPIFFS_MOUNT_POINT, filename);
    if (written <= 0 || written >= (int)sizeof(path)) {
        ESP_LOGW(TAG, "SPIFFS path truncated for '%s'", filename);
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGD(TAG, "SPIFFS override '%s' not found", path);
        return;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return;
    }
    rewind(f);

    uint8_t *buffer = (uint8_t *)malloc((size_t)size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Out of memory loading '%s' from SPIFFS", path);
        fclose(f);
        return;
    }
    size_t read = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        ESP_LOGW(TAG, "Short read for '%s' (%zu/%ld)", path, read, size);
        free(buffer);
        return;
    }
    buffer[read] = '\0';
    blob->override = buffer;
    blob->override_len = read;
    ESP_LOGI(TAG, "Loaded %zu B override from SPIFFS '%s'", read, path);
#else
    (void)filename;
    (void)blob;
#endif
}

static void cert_store_init_once(void)
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;

    cert_store_load_from_nvs("server_cert", &s_server_cert);
    cert_store_load_from_nvs("server_key", &s_server_key);
    cert_store_load_from_nvs("ca_cert", &s_ca_cert);

    cert_store_load_from_spiffs("server_cert.pem", &s_server_cert);
    cert_store_load_from_spiffs("server_key.pem", &s_server_key);
    cert_store_load_from_spiffs("ca_cert.pem", &s_ca_cert);
}

static const uint8_t *cert_store_select_blob(cert_store_blob_t *blob, size_t *len)
{
    cert_store_init_once();
    if (blob->override && blob->override_len > 0) {
        if (len) {
            *len = blob->override_len;
        }
        return blob->override;
    }
    if (len) {
        *len = blob->embedded_len;
    }
    return blob->embedded;
}

const uint8_t *cert_store_server_cert(size_t *len)
{
    return cert_store_select_blob(&s_server_cert, len);
}

const uint8_t *cert_store_server_key(size_t *len)
{
    return cert_store_select_blob(&s_server_key, len);
}

const uint8_t *cert_store_ca_cert(size_t *len)
{
    return cert_store_select_blob(&s_ca_cert, len);
}
