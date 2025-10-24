#include "cert_store.h"

extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");
extern const uint8_t server_key_pem_start[] asm("_binary_server_key_pem_start");
extern const uint8_t server_key_pem_end[] asm("_binary_server_key_pem_end");
extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t ca_cert_pem_end[] asm("_binary_ca_cert_pem_end");

const uint8_t *cert_store_server_cert(size_t *len)
{
    if (len) {
        *len = (size_t)(server_cert_pem_end - server_cert_pem_start);
    }
    return server_cert_pem_start;
}

const uint8_t *cert_store_server_key(size_t *len)
{
    if (len) {
        *len = (size_t)(server_key_pem_end - server_key_pem_start);
    }
    return server_key_pem_start;
}

const uint8_t *cert_store_ca_cert(size_t *len)
{
    if (len) {
        *len = (size_t)(ca_cert_pem_end - ca_cert_pem_start);
    }
    return ca_cert_pem_start;
}
