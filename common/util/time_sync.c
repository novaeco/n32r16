#include "time_sync.h"

#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

static const char *TAG = "time_sync";
static bool s_initialized;
static volatile bool s_time_synced;

static void time_sync_notification_cb(struct timeval *tv) {
    (void)tv;
    s_time_synced = true;
    ESP_LOGI(TAG, "System time synchronized");
}

void time_sync_init(void) {
    if (s_initialized) {
        return;
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();
    s_initialized = true;
}

uint64_t time_sync_get_monotonic_ms(void) {
    return esp_timer_get_time() / 1000ULL;
}

uint64_t time_sync_get_unix_ms(void) {
    struct timeval now = {0};
    gettimeofday(&now, NULL);
    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_usec / 1000ULL;
}

bool time_sync_is_synchronized(void) {
    if (!s_initialized) {
        return false;
    }
    if (s_time_synced) {
        return true;
    }
    sntp_sync_status_t status = sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        s_time_synced = true;
    }
    return s_time_synced;
}

