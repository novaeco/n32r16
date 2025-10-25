#include "memory_profile.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "mem_profile";

static memory_profile_t s_profile;
static bool s_initialised;

esp_err_t memory_profile_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }
    memset(&s_profile, 0, sizeof(s_profile));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    s_profile.chip_model = chip.model;
    s_profile.chip_revision = chip.revision;
    s_profile.cores = chip.cores;

    uint32_t flash_bytes = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_bytes);
    if (flash_err == ESP_OK) {
        s_profile.flash_size_bytes = flash_bytes;
    }

#if CONFIG_SPIRAM
    s_profile.psram_available = esp_psram_is_initialized();
    if (s_profile.psram_available) {
        s_profile.psram_size_bytes = esp_psram_get_size();
        multi_heap_info_t info = {0};
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
        s_profile.psram_free_bytes = info.total_free_bytes;
    }
#else
    s_profile.psram_available = esp_psram_is_initialized();
    if (s_profile.psram_available) {
        s_profile.psram_size_bytes = esp_psram_get_size();
        multi_heap_info_t info = {0};
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
        s_profile.psram_free_bytes = info.total_free_bytes;
    }
#endif

    multi_heap_info_t internal_info = {0};
    heap_caps_get_info(&internal_info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_profile.internal_ram_total_bytes = internal_info.total_allocated_bytes + internal_info.total_free_bytes;
    s_profile.internal_ram_free_bytes = internal_info.total_free_bytes;

    s_initialised = true;
    return ESP_OK;
}

const memory_profile_t *memory_profile_get(void)
{
    if (!s_initialised) {
        if (memory_profile_init() != ESP_OK) {
            return NULL;
        }
    }
    return &s_profile;
}

void memory_profile_log(void)
{
    const memory_profile_t *profile = memory_profile_get();
    if (!profile) {
        ESP_LOGW(TAG, "Memory profile unavailable");
        return;
    }
    ESP_LOGI(TAG,
             "chip=%" PRIu32 " rev=%" PRIu32 " cores=%" PRIu32 " flash=%u KiB psram=%u KiB (available=%s) internal_free=%u KiB",
             profile->chip_model, profile->chip_revision, profile->cores,
             (unsigned)(profile->flash_size_bytes / 1024U),
             (unsigned)(profile->psram_size_bytes / 1024U),
             profile->psram_available ? "yes" : "no",
             (unsigned)(profile->internal_ram_free_bytes / 1024U));
}

size_t memory_profile_recommend_draw_buffer_px(uint32_t hor_res, uint32_t ver_res, uint32_t min_lines)
{
    const memory_profile_t *profile = memory_profile_get();
    if (!profile) {
        return (size_t)hor_res * min_lines;
    }
    size_t frame_pixels = (size_t)hor_res * (size_t)ver_res;
    size_t min_pixels = (size_t)hor_res * (size_t)min_lines;

    if (!profile->psram_available || profile->psram_size_bytes == 0) {
        size_t capped = frame_pixels / 8U;
        if (capped < min_pixels) {
            capped = min_pixels;
        }
        return capped;
    }

    if (profile->psram_size_bytes >= 16 * 1024 * 1024) {
        return frame_pixels;
    }
    if (profile->psram_size_bytes >= 8 * 1024 * 1024) {
        size_t half = frame_pixels / 2U;
        return half > min_pixels ? half : min_pixels;
    }
    size_t quarter = frame_pixels / 4U;
    if (quarter < min_pixels) {
        quarter = min_pixels;
    }
    return quarter;
}

