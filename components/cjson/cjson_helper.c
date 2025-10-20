#include "cjson_helper.h"

#include <stdlib.h>

bool json_doc_init(json_doc_t *doc) {
    if (doc == NULL) {
        return false;
    }
    doc->root = cJSON_CreateObject();
    return doc->root != NULL;
}

void json_doc_free(json_doc_t *doc) {
    if (doc == NULL || doc->root == NULL) {
        return;
    }
    cJSON_Delete(doc->root);
    doc->root = NULL;
}

bool json_doc_set_string(json_doc_t *doc, const char *key, const char *value) {
    if (doc == NULL || doc->root == NULL || key == NULL || value == NULL) {
        return false;
    }
    cJSON *item = cJSON_CreateString(value);
    if (item == NULL) {
        return false;
    }
    cJSON_ReplaceItemInObject(doc->root, key, item);
    return true;
}

bool json_doc_set_number(json_doc_t *doc, const char *key, double value) {
    if (doc == NULL || doc->root == NULL || key == NULL) {
        return false;
    }
    cJSON *item = cJSON_CreateNumber(value);
    if (item == NULL) {
        return false;
    }
    cJSON_ReplaceItemInObject(doc->root, key, item);
    return true;
}

bool json_doc_set_uint(json_doc_t *doc, const char *key, uint64_t value) {
    if (doc == NULL || doc->root == NULL || key == NULL) {
        return false;
    }
    cJSON *item = cJSON_CreateNumber((double)value);
    if (item == NULL) {
        return false;
    }
    cJSON_ReplaceItemInObject(doc->root, key, item);
    return true;
}

bool json_doc_set_object(json_doc_t *doc, const char *key, cJSON *object) {
    if (doc == NULL || doc->root == NULL || key == NULL || object == NULL) {
        return false;
    }
    cJSON_ReplaceItemInObject(doc->root, key, object);
    return true;
}

cJSON *json_doc_get_root(const json_doc_t *doc) {
    if (doc == NULL) {
        return NULL;
    }
    return doc->root;
}

char *json_doc_print_unformatted(const json_doc_t *doc) {
    if (doc == NULL || doc->root == NULL) {
        return NULL;
    }
    return cJSON_PrintUnformatted(doc->root);
}

