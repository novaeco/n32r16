#include "time_sync.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

static const char *TAG = "time_sync";

static bool s_time_synced = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    ESP_LOGI(TAG, "Time synchronized");
}

esp_err_t time_sync_start(const char *server)
{
    sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (server && server[0] != '\0') {
        sntp_setservername(0, (char *)server);
    } else {
        sntp_setservername(0, "pool.ntp.org");
    }
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
    return ESP_OK;
}

int64_t time_sync_get_epoch_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000LL) + (tv.tv_usec / 1000);
}
