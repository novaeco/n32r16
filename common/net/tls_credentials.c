#include "tls_credentials.h"

#include "esp_log.h"
#include "esp_secure_cert_read.h"

static const char *TAG = "tls_creds";

esp_err_t tls_credentials_load_server(tls_server_credentials_t *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *cert = NULL;
    size_t cert_len = 0;
    esp_err_t err = esp_secure_cert_read_x509_cert(&cert, &cert_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read X509 cert: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t *key = NULL;
    size_t key_len = 0;
    err = esp_secure_cert_read_private_key(&key, &key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read private key: %s", esp_err_to_name(err));
        esp_secure_cert_free_x509_cert(cert);
        return err;
    }
    out->certificate = cert;
    out->certificate_len = cert_len;
    out->private_key = key;
    out->private_key_len = key_len;
    return ESP_OK;
}

void tls_credentials_release(tls_server_credentials_t *creds) {
    if (creds == NULL) {
        return;
    }
    if (creds->certificate != NULL) {
        esp_secure_cert_free_x509_cert(creds->certificate);
        creds->certificate = NULL;
        creds->certificate_len = 0;
    }
    if (creds->private_key != NULL) {
        esp_secure_cert_free_private_key(creds->private_key);
        creds->private_key = NULL;
        creds->private_key_len = 0;
    }
}

