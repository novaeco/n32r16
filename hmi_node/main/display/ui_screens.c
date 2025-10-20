#include "display/ui_screens.h"

#include <stdio.h>

#include "cJSON.h"
#include "data_model.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "net/ws_client.h"
#include "proto_codec.h"
#include "time_sync.h"

static const char *TAG = "ui";
static lv_obj_t *s_tabview;
static lv_obj_t *s_status_label;
static lv_obj_t *s_crc_label;
static lv_obj_t *s_rssi_label;
static lv_obj_t *s_sht_labels[HMI_MAX_SHT20];
static lv_obj_t *s_ds_labels[HMI_MAX_DS18B20];
static lv_obj_t *s_gpio_switches[2][2][8];
static lv_obj_t *s_gpio_labels[2];
static lv_obj_t *s_pwm_sliders[16];
static lv_obj_t *s_pwm_freq_slider;
static lv_obj_t *s_pwm_freq_label;
static lv_chart_series_t *s_temp_series;
static lv_obj_t *s_chart;
static bool s_ui_updating;

static bool send_command_payload(cJSON *payload) {
    if (payload == NULL) {
        return false;
    }
    proto_buffer_t buf = {0};
    uint32_t seq = hmi_data_model_next_command_seq();
    uint32_t crc = 0;
    bool ok = proto_encode_command(payload, time_sync_get_monotonic_ms(), seq, &buf, &crc);
    if (ok) {
        hmi_data_model_register_command(seq);
        esp_err_t err = hmi_ws_client_send(&buf);
        ok = (err == ESP_OK);
        if (!ok) {
            ESP_LOGW(TAG, "Command send failed: %s", esp_err_to_name(err));
        }
    }
    proto_buffer_free(&buf);
    cJSON_Delete(payload);
    return ok;
}

static void gpio_switch_event_cb(lv_event_t *e) {
    if (s_ui_updating) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    uint32_t encoded = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t device = (encoded >> 8) & 0xFF;
    uint8_t port = (encoded >> 4) & 0x0F;
    uint8_t pin = encoded & 0x0F;
    uint8_t mask = (1u << pin);
    uint8_t value = lv_obj_has_state(sw, LV_STATE_CHECKED) ? mask : 0;
    cJSON *payload = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "write_gpio", cmd);
    cJSON_AddStringToObject(cmd, "dev", device == 0 ? "mcp0" : "mcp1");
    cJSON_AddStringToObject(cmd, "port", port == 0 ? "A" : "B");
    cJSON_AddNumberToObject(cmd, "mask", mask);
    cJSON_AddNumberToObject(cmd, "value", value);
    send_command_payload(payload);
}

static void pwm_slider_event_cb(lv_event_t *e) {
    if (s_ui_updating) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    if (!lv_obj_has_state(slider, LV_STATE_PRESSED)) {
        return;
    }
    uint8_t channel = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint16_t duty = (uint16_t)lv_slider_get_value(slider);
    cJSON *payload = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "set_pwm", cmd);
    cJSON_AddNumberToObject(cmd, "ch", channel);
    cJSON_AddNumberToObject(cmd, "duty", duty);
    send_command_payload(payload);
}

static void pwm_freq_event_cb(lv_event_t *e) {
    if (s_ui_updating) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    if (!lv_obj_has_state(slider, LV_STATE_PRESSED)) {
        return;
    }
    uint16_t freq = (uint16_t)lv_slider_get_value(slider);
    cJSON *payload = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "set_pwm_freq", cmd);
    cJSON_AddNumberToObject(cmd, "freq", freq);
    send_command_payload(payload);
}

static lv_obj_t *create_sensor_card(lv_obj_t *parent, const char *title) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 240, 140);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x222831), 0);
    lv_obj_set_style_text_color(cont, lv_color_hex(0xE8E8E8), 0);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);
    return cont;
}

static void build_dashboard_tab(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x1E1E1E), 0);

    s_status_label = lv_label_create(tab);
    lv_label_set_text(s_status_label, "Link: --");
    s_crc_label = lv_label_create(tab);
    lv_label_set_text(s_crc_label, "CRC: --");
    s_rssi_label = lv_label_create(tab);
    lv_label_set_text(s_rssi_label, "RSSI: -- dBm");

    for (size_t i = 0; i < HMI_MAX_SHT20; ++i) {
        char title[32];
        snprintf(title, sizeof(title), "SHT20 %zu", i + 1);
        lv_obj_t *card = create_sensor_card(tab, title);
        s_sht_labels[i] = lv_label_create(card);
        lv_obj_align(s_sht_labels[i], LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_sht_labels[i], "--.- °C\n--.- %RH");
    }
    for (size_t i = 0; i < HMI_MAX_DS18B20; ++i) {
        char title[32];
        snprintf(title, sizeof(title), "DS18B20 %zu", i + 1);
        lv_obj_t *card = create_sensor_card(tab, title);
        s_ds_labels[i] = lv_label_create(card);
        lv_obj_align(s_ds_labels[i], LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_ds_labels[i], "--.- °C");
    }

    s_chart = lv_chart_create(tab);
    lv_obj_set_size(s_chart, 480, 200);
    lv_chart_set_point_count(s_chart, 128);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    s_temp_series = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
}

static void build_gpio_tab(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    for (int dev = 0; dev < 2; ++dev) {
        for (int port = 0; port < 2; ++port) {
            lv_obj_t *row = lv_obj_create(tab);
            lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
            char title[32];
            snprintf(title, sizeof(title), "MCP%d Port %c", dev, port == 0 ? 'A' : 'B');
            lv_obj_t *label = lv_label_create(row);
            lv_label_set_text(label, title);
            s_gpio_labels[dev] = label;
            for (int pin = 0; pin < 8; ++pin) {
                lv_obj_t *sw = lv_switch_create(row);
                lv_obj_add_event_cb(sw, gpio_switch_event_cb, LV_EVENT_VALUE_CHANGED,
                                     (void *)(uintptr_t)((dev << 8) | (port << 4) | pin));
                s_gpio_switches[dev][port][pin] = sw;
            }
        }
    }
}

static void build_pwm_tab(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    s_pwm_freq_label = lv_label_create(tab);
    lv_label_set_text(s_pwm_freq_label, "Frequency: 0 Hz");
    s_pwm_freq_slider = lv_slider_create(tab);
    lv_slider_set_range(s_pwm_freq_slider, 50, 1600);
    lv_slider_set_value(s_pwm_freq_slider, 500, LV_ANIM_OFF);
    lv_obj_set_width(s_pwm_freq_slider, 400);
    lv_obj_add_event_cb(s_pwm_freq_slider, pwm_freq_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    for (int ch = 0; ch < 16; ++ch) {
        lv_obj_t *slider = lv_slider_create(tab);
        lv_slider_set_range(slider, 0, 4095);
        lv_obj_set_width(slider, 400);
        lv_obj_add_event_cb(slider, pwm_slider_event_cb, LV_EVENT_VALUE_CHANGED,
                             (void *)(uintptr_t)ch);
        s_pwm_sliders[ch] = slider;
    }
}

void ui_screens_init(void) {
    s_tabview = lv_tabview_create(lv_screen_active(), LV_DIR_TOP, 60);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x121212), 0);

    lv_obj_t *dashboard = lv_tabview_add_tab(s_tabview, "Dashboard");
    build_dashboard_tab(dashboard);

    lv_obj_t *gpio_tab = lv_tabview_add_tab(s_tabview, "GPIO");
    build_gpio_tab(gpio_tab);

    lv_obj_t *pwm_tab = lv_tabview_add_tab(s_tabview, "PWM");
    build_pwm_tab(pwm_tab);

    lv_obj_t *settings_tab = lv_tabview_add_tab(s_tabview, "Settings");
    lv_label_set_text(lv_label_create(settings_tab), "Configure Wi-Fi via menuconfig\nWebSocket uses mDNS discovery");
}

static void update_status_labels(const hmi_data_snapshot_t *snapshot) {
    char status[64];
    snprintf(status, sizeof(status), "Link: %s", snapshot->link_up ? "OK" : "Down");
    lv_label_set_text(s_status_label, status);

    char crc[64];
    snprintf(crc, sizeof(crc), "CRC: %s (seq %lu)", snapshot->crc_ok ? "OK" : "ERR",
             (unsigned long)snapshot->last_seq);
    lv_label_set_text(s_crc_label, crc);

    char rssi[64];
    snprintf(rssi, sizeof(rssi), "RSSI: %d dBm, CPU %.1f%%", snapshot->wifi_rssi, snapshot->cpu_percent);
    lv_label_set_text(s_rssi_label, rssi);
}

static void update_sensor_cards(const hmi_data_snapshot_t *snapshot) {
    for (size_t i = 0; i < HMI_MAX_SHT20; ++i) {
        if (!snapshot->sht20[i].valid) {
            continue;
        }
        char text[64];
        snprintf(text, sizeof(text), "%s\n%.2f °C\n%.2f %%RH", snapshot->sht20[i].id,
                 snapshot->sht20[i].temperature_c, snapshot->sht20[i].humidity_rh);
        lv_label_set_text(s_sht_labels[i], text);
    }
    for (size_t i = 0; i < HMI_MAX_DS18B20; ++i) {
        if (!snapshot->ds18b20[i].valid) {
            continue;
        }
        char text[48];
        snprintf(text, sizeof(text), "%s\n%.2f °C", snapshot->ds18b20[i].rom,
                 snapshot->ds18b20[i].temperature_c);
        lv_label_set_text(s_ds_labels[i], text);
        lv_chart_set_next_value(s_chart, s_temp_series, snapshot->ds18b20[i].temperature_c);
    }
}

static void update_gpio_controls(const hmi_data_snapshot_t *snapshot) {
    for (int dev = 0; dev < 2; ++dev) {
        for (int port = 0; port < 2; ++port) {
            uint8_t value = snapshot->gpio_state[dev][port];
            for (int pin = 0; pin < 8; ++pin) {
                if (s_gpio_switches[dev][port][pin] == NULL) {
                    continue;
                }
                bool state = (value >> pin) & 0x1;
                if (state) {
                    lv_obj_add_state(s_gpio_switches[dev][port][pin], LV_STATE_CHECKED);
                } else {
                    lv_obj_clear_state(s_gpio_switches[dev][port][pin], LV_STATE_CHECKED);
                }
            }
        }
    }
}

static void update_pwm_controls(const hmi_data_snapshot_t *snapshot) {
    char freq_text[32];
    snprintf(freq_text, sizeof(freq_text), "Frequency: %u Hz", snapshot->pwm_freq);
    lv_label_set_text(s_pwm_freq_label, freq_text);
    lv_slider_set_value(s_pwm_freq_slider, snapshot->pwm_freq, LV_ANIM_OFF);
    for (int ch = 0; ch < 16; ++ch) {
        lv_slider_set_value(s_pwm_sliders[ch], snapshot->pwm_duty[ch], LV_ANIM_OFF);
    }
}

void ui_screens_update(const hmi_data_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    s_ui_updating = true;
    update_status_labels(snapshot);
    update_sensor_cards(snapshot);
    update_gpio_controls(snapshot);
    update_pwm_controls(snapshot);
    s_ui_updating = false;
}

