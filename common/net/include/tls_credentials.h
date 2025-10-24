#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *certificate;
    size_t certificate_len;
    uint8_t *private_key;
    size_t private_key_len;
} tls_server_credentials_t;

esp_err_t tls_credentials_load_server(tls_server_credentials_t *out);
void tls_credentials_release(tls_server_credentials_t *creds);

#ifdef __cplusplus
}
#endif

