#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    cJSON *root;
} json_doc_t;

bool json_doc_init(json_doc_t *doc);
void json_doc_free(json_doc_t *doc);

bool json_doc_set_string(json_doc_t *doc, const char *key, const char *value);
bool json_doc_set_number(json_doc_t *doc, const char *key, double value);
bool json_doc_set_uint(json_doc_t *doc, const char *key, uint64_t value);
bool json_doc_set_object(json_doc_t *doc, const char *key, cJSON *object);

cJSON *json_doc_get_root(const json_doc_t *doc);
char *json_doc_print_unformatted(const json_doc_t *doc);

#ifdef __cplusplus
}
#endif

