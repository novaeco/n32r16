#include "display/ui_screens.h"

#include <stdio.h>

static lv_obj_t *s_tabview;
static lv_obj_t *s_sht_labels[HMI_MAX_SHT20];
static lv_obj_t *s_ds_labels[HMI_MAX_DS18B20];
static lv_obj_t *s_gpio_labels[2][2];
static lv_obj_t *s_pwm_sliders[16];
static lv_obj_t *s_pwm_freq_label;
static lv_chart_series_t *s_temp_series;
static lv_obj_t *s_chart;

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

void ui_screens_init(void) {
    s_tabview = lv_tabview_create(lv_screen_active(), LV_DIR_TOP, 60);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x121212), 0);

    lv_obj_t *dashboard = lv_tabview_add_tab(s_tabview, "Dashboard");
    lv_obj_set_flex_flow(dashboard, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(dashboard, 12, 0);
    lv_obj_set_style_bg_color(dashboard, lv_color_hex(0x1E1E1E), 0);

    for (size_t i = 0; i < HMI_MAX_SHT20; ++i) {
        char title[32];
        snprintf(title, sizeof(title), "SHT20 %zu", i + 1);
        lv_obj_t *card = create_sensor_card(dashboard, title);
        s_sht_labels[i] = lv_label_create(card);
        lv_obj_align(s_sht_labels[i], LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_sht_labels[i], "--.- °C\n--.- %RH");
    }
    for (size_t i = 0; i < HMI_MAX_DS18B20; ++i) {
        char title[32];
        snprintf(title, sizeof(title), "DS18B20 %zu", i + 1);
        lv_obj_t *card = create_sensor_card(dashboard, title);
        s_ds_labels[i] = lv_label_create(card);
        lv_obj_align(s_ds_labels[i], LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_ds_labels[i], "--.- °C");
    }

    s_chart = lv_chart_create(dashboard);
    lv_obj_set_size(s_chart, 480, 200);
    lv_chart_set_point_count(s_chart, 128);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    s_temp_series = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *gpio_tab = lv_tabview_add_tab(s_tabview, "GPIO");
    lv_obj_set_flex_flow(gpio_tab, LV_FLEX_FLOW_COLUMN);
    for (int dev = 0; dev < 2; ++dev) {
        for (int port = 0; port < 2; ++port) {
            lv_obj_t *label = lv_label_create(gpio_tab);
            char text[32];
            snprintf(text, sizeof(text), "MCP%d Port %c: 0x00", dev, port == 0 ? 'A' : 'B');
            lv_label_set_text(label, text);
            s_gpio_labels[dev][port] = label;
        }
    }

    lv_obj_t *pwm_tab = lv_tabview_add_tab(s_tabview, "PWM");
    lv_obj_set_flex_flow(pwm_tab, LV_FLEX_FLOW_COLUMN);
    s_pwm_freq_label = lv_label_create(pwm_tab);
    lv_label_set_text(s_pwm_freq_label, "Frequency: 0 Hz");
    for (int ch = 0; ch < 16; ++ch) {
        lv_obj_t *slider = lv_slider_create(pwm_tab);
        lv_slider_set_range(slider, 0, 4095);
        lv_obj_set_width(slider, 400);
        s_pwm_sliders[ch] = slider;
    }

    lv_obj_t *settings_tab = lv_tabview_add_tab(s_tabview, "Settings");
    lv_label_set_text(lv_label_create(settings_tab), "Wi-Fi STA mode\nEdit credentials via menuconfig");
}

static void update_sensor_labels(const hmi_data_snapshot_t *snapshot) {
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

static void update_gpio_labels(const hmi_data_snapshot_t *snapshot) {
    for (int dev = 0; dev < 2; ++dev) {
        for (int port = 0; port < 2; ++port) {
            char text[32];
            snprintf(text, sizeof(text), "MCP%d Port %c: 0x%02X", dev, port == 0 ? 'A' : 'B',
                     snapshot->gpio_state[dev][port]);
            lv_label_set_text(s_gpio_labels[dev][port], text);
        }
    }
}

static void update_pwm_controls(const hmi_data_snapshot_t *snapshot) {
    char freq_text[32];
    snprintf(freq_text, sizeof(freq_text), "Frequency: %u Hz", snapshot->pwm_freq);
    lv_label_set_text(s_pwm_freq_label, freq_text);
    for (int ch = 0; ch < 16; ++ch) {
        lv_slider_set_value(s_pwm_sliders[ch], snapshot->pwm_duty[ch], LV_ANIM_OFF);
    }
}

void ui_screens_update(const hmi_data_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    update_sensor_labels(snapshot);
    update_gpio_labels(snapshot);
    update_pwm_controls(snapshot);
}

