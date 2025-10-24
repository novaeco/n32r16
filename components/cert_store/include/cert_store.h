#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *cert_store_server_cert(size_t *len);
const uint8_t *cert_store_server_key(size_t *len);
const uint8_t *cert_store_ca_cert(size_t *len);

#ifdef __cplusplus
}
#endif
