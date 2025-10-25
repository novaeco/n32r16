#include "display/ui_screens.h"

#include "display/lvgl_port.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define UI_HISTORY_POINTS 512
#define PWM_CHANNEL_COUNT 16
#define GPIO_DEVICE_COUNT 2
#define GPIO_PINS_PER_DEVICE 16

typedef struct {
    uint8_t device_index;
    uint8_t pin_index;
} gpio_switch_ctx_t;

typedef struct {
    uint8_t channel;
    lv_obj_t *value_label;
} pwm_slider_ctx_t;

static ui_callbacks_t s_callbacks;
static void *s_callback_ctx;
static bool s_updating_ui;
static hmi_user_preferences_t s_active_prefs;

static lv_obj_t *s_status_label;
static lv_obj_t *s_crc_badge;
static lv_obj_t *s_wifi_badge;
static lv_obj_t *s_dashboard_tab;
static lv_obj_t *s_gpio_tab;
static lv_obj_t *s_pwm_tab;
static lv_obj_t *s_traces_tab;
static lv_obj_t *s_settings_tab;

static lv_obj_t *s_sht20_name[2];
static lv_obj_t *s_sht20_temp[2];
static lv_obj_t *s_sht20_hum[2];
static lv_obj_t *s_ds18_name[4];
static lv_obj_t *s_ds18_temp[4];

static lv_obj_t *s_gpio_switch[GPIO_DEVICE_COUNT][GPIO_PINS_PER_DEVICE];
static lv_obj_t *s_gpio_state_label[GPIO_DEVICE_COUNT][GPIO_PINS_PER_DEVICE];
static gpio_switch_ctx_t s_gpio_ctx[GPIO_DEVICE_COUNT][GPIO_PINS_PER_DEVICE];

static lv_obj_t *s_pwm_slider[PWM_CHANNEL_COUNT];
static lv_obj_t *s_pwm_label[PWM_CHANNEL_COUNT];
static pwm_slider_ctx_t s_pwm_ctx[PWM_CHANNEL_COUNT];
static lv_obj_t *s_pwm_freq_slider;
static lv_obj_t *s_pwm_freq_label;

static lv_obj_t *s_chart_temp;
static lv_obj_t *s_chart_hum;
static lv_obj_t *s_chart_ds;
static lv_chart_series_t *s_temp_series[2];
static lv_chart_series_t *s_hum_series[2];
static lv_chart_series_t *s_ds_series[4];

static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_password_ta;
static lv_obj_t *s_mdns_ta;
static lv_obj_t *s_theme_switch;
static lv_obj_t *s_units_switch;

static void apply_theme(bool dark)
{
    lvgl_port_lock();
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_darken(LV_PALETTE_GREY, 2), dark, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, theme);
    lvgl_port_unlock();
}

static void format_rom_code(const uint8_t *rom_code, char *out, size_t len)
{
    if (!rom_code || len < 17) {
        if (len > 0) {
            out[0] = '\0';
        }
        return;
    }
    out[0] = '\0';
    for (size_t i = 0; i < 8 && (i * 2 + 1) < len; ++i) {
        snprintf(out + i * 2, len - i * 2, "%02X", rom_code[i]);
    }
}

static const char *format_temperature(char *buf, size_t len, float celsius, bool use_fahrenheit)
{
    if (!isfinite(celsius)) {
        if (len > 0) {
            snprintf(buf, len, "--");
        }
        return buf;
    }
    float value = celsius;
    const char *unit = "°C";
    if (use_fahrenheit) {
        value = (celsius * 9.0f / 5.0f) + 32.0f;
        unit = "°F";
    }
    snprintf(buf, len, "%.2f %s", value, unit);
    return buf;
}

static void dispatch_gpio_write(const gpio_switch_ctx_t *ctx, bool on)
{
    if (!ctx || !s_callbacks.write_gpio) {
        return;
    }
    uint8_t port = ctx->pin_index < 8 ? 0 : 1;
    uint8_t bit = ctx->pin_index % 8;
    uint16_t mask = (uint16_t)(1U << bit);
    uint16_t value = on ? mask : 0;
    s_callbacks.write_gpio(ctx->device_index, port, mask, value, s_callback_ctx);
}

static void dispatch_pwm_value(pwm_slider_ctx_t *ctx, int16_t value)
{
    if (!ctx) {
        return;
    }
    if (ctx->value_label) {
        char text[16];
        snprintf(text, sizeof(text), "%d", value);
        lv_label_set_text(ctx->value_label, text);
    }
    if (s_callbacks.set_pwm) {
        s_callbacks.set_pwm(ctx->channel, (uint16_t)value, s_callback_ctx);
    }
}

static void dispatch_pwm_frequency(uint16_t freq)
{
    if (s_pwm_freq_label) {
        char text[16];
        snprintf(text, sizeof(text), "%u Hz", freq);
        lv_label_set_text(s_pwm_freq_label, text);
    }
    if (s_callbacks.set_pwm_frequency) {
        s_callbacks.set_pwm_frequency(freq, s_callback_ctx);
    }
}

static void dispatch_apply_preferences(const char *ssid, const char *password, const char *mdns, bool dark,
                                       bool fahrenheit)
{
    if (!s_callbacks.apply_preferences) {
        return;
    }
    hmi_user_preferences_t prefs = s_active_prefs;
    if (ssid) {
        strncpy(prefs.ssid, ssid, sizeof(prefs.ssid) - 1);
        prefs.ssid[sizeof(prefs.ssid) - 1] = '\0';
    }
    if (password) {
        strncpy(prefs.password, password, sizeof(prefs.password) - 1);
        prefs.password[sizeof(prefs.password) - 1] = '\0';
    }
    if (mdns) {
        strncpy(prefs.mdns_target, mdns, sizeof(prefs.mdns_target) - 1);
        prefs.mdns_target[sizeof(prefs.mdns_target) - 1] = '\0';
    }
    prefs.dark_theme = dark;
    prefs.use_fahrenheit = fahrenheit;
    s_callbacks.apply_preferences(&prefs, s_callback_ctx);
}

static void dispatch_reset_preferences(void)
{
    if (!s_callbacks.reset_preferences) {
        return;
    }
    s_callbacks.reset_preferences(s_callback_ctx);
}

static void gpio_switch_event_cb(lv_event_t *e)
{
    if (s_updating_ui) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    const gpio_switch_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }
    lv_obj_t *target = lv_event_get_target(e);
    bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
    dispatch_gpio_write(ctx, on);
}

static void pwm_slider_event_cb(lv_event_t *e)
{
    if (s_updating_ui) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }
    pwm_slider_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);
    dispatch_pwm_value(ctx, value);
}

static void pwm_freq_event_cb(lv_event_t *e)
{
    if (s_updating_ui) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED && lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    if (!s_callbacks.set_pwm_frequency) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    uint16_t freq = (uint16_t)lv_slider_get_value(slider);
    dispatch_pwm_frequency(freq);
}

static void prefs_apply_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *password = lv_textarea_get_text(s_password_ta);
    const char *mdns = lv_textarea_get_text(s_mdns_ta);
    bool dark = lv_obj_has_state(s_theme_switch, LV_STATE_CHECKED);
    bool fahrenheit = lv_obj_has_state(s_units_switch, LV_STATE_CHECKED);
    dispatch_apply_preferences(ssid, password, mdns, dark, fahrenheit);
}

static void prefs_reset_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    dispatch_reset_preferences();
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 320, 120);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xA0D8FF), 0);
    return card;
}

static void create_dashboard_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_gap(tab, 16, 0);

    lv_obj_t *status_row = lv_obj_create(tab);
    lv_obj_set_size(status_row, LV_PCT(100), 60);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(status_row, 16, 0);

    s_status_label = lv_label_create(status_row);
    lv_label_set_text(s_status_label, "Wi-Fi: Disconnected");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFF5555), 0);

    s_wifi_badge = lv_label_create(status_row);
    lv_label_set_text(s_wifi_badge, "RSSI: -- dBm");
    lv_obj_set_style_text_color(s_wifi_badge, lv_color_hex(0xAAAAAA), 0);

    s_crc_badge = lv_label_create(status_row);
    lv_label_set_text(s_crc_badge, "CRC: n/a");
    lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t *sht_row = lv_obj_create(tab);
    lv_obj_set_size(sht_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sht_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sht_row, 0, 0);
    lv_obj_set_style_pad_all(sht_row, 0, 0);
    lv_obj_set_flex_flow(sht_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(sht_row, 16, 0);

    for (size_t i = 0; i < 2; ++i) {
        lv_obj_t *card = create_card(sht_row, "SHT20");
        s_sht20_name[i] = lv_label_create(card);
        lv_label_set_text_fmt(s_sht20_name[i], "Sensor %u", (unsigned)(i + 1));
        s_sht20_temp[i] = lv_label_create(card);
        lv_label_set_text(s_sht20_temp[i], "Temp: --");
        s_sht20_hum[i] = lv_label_create(card);
        lv_label_set_text(s_sht20_hum[i], "RH: --");
    }

    lv_obj_t *ds_row = lv_obj_create(tab);
    lv_obj_set_size(ds_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ds_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ds_row, 0, 0);
    lv_obj_set_style_pad_all(ds_row, 0, 0);
    lv_obj_set_flex_flow(ds_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(ds_row, 16, 0);

    for (size_t i = 0; i < 4; ++i) {
        lv_obj_t *card = create_card(ds_row, "DS18B20");
        s_ds18_name[i] = lv_label_create(card);
        lv_label_set_text_fmt(s_ds18_name[i], "ROM %u", (unsigned)(i + 1));
        s_ds18_temp[i] = lv_label_create(card);
        lv_label_set_text(s_ds18_temp[i], "Temp: --");
    }
}

static void create_gpio_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_gap(tab, 24, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t dev = 0; dev < GPIO_DEVICE_COUNT; ++dev) {
        lv_obj_t *panel = lv_obj_create(tab);
        lv_obj_set_size(panel, 480, 300);
        lv_obj_set_style_pad_all(panel, 16, 0);
        lv_obj_set_style_pad_gap(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_t *title = lv_label_create(panel);
        lv_label_set_text_fmt(title, "MCP23017_%u", (unsigned)dev);
        lv_obj_set_width(title, LV_PCT(100));

        for (uint8_t pin = 0; pin < GPIO_PINS_PER_DEVICE; ++pin) {
            lv_obj_t *cell = lv_obj_create(panel);
            lv_obj_set_size(cell, 100, 100);
            lv_obj_set_style_pad_all(cell, 6, 0);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_gap(cell, 4, 0);

            lv_obj_t *label = lv_label_create(cell);
            lv_label_set_text_fmt(label, "Pin %u", (unsigned)pin);

            gpio_switch_ctx_t *ctx = &s_gpio_ctx[dev][pin];
            ctx->device_index = dev;
            ctx->pin_index = pin;

            lv_obj_t *sw = lv_switch_create(cell);
            lv_obj_add_event_cb(sw, gpio_switch_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
            s_gpio_switch[dev][pin] = sw;

            lv_obj_t *state = lv_label_create(cell);
            lv_label_set_text(state, "OFF");
            s_gpio_state_label[dev][pin] = state;
        }
    }
}

static void create_pwm_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_gap(tab, 16, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *freq_row = lv_obj_create(tab);
    lv_obj_set_width(freq_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(freq_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(freq_row, 0, 0);
    lv_obj_set_style_pad_all(freq_row, 0, 0);
    lv_obj_set_flex_flow(freq_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(freq_row, 12, 0);

    lv_obj_t *freq_label = lv_label_create(freq_row);
    lv_label_set_text(freq_label, "Frequency");

    s_pwm_freq_slider = lv_slider_create(freq_row);
    lv_slider_set_range(s_pwm_freq_slider, 40, 2000);
    lv_obj_set_size(s_pwm_freq_slider, 300, 20);
    lv_obj_add_event_cb(s_pwm_freq_slider, pwm_freq_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_pwm_freq_slider, pwm_freq_event_cb, LV_EVENT_RELEASED, NULL);

    s_pwm_freq_label = lv_label_create(freq_row);
    lv_label_set_text(s_pwm_freq_label, "500 Hz");

    lv_obj_t *grid = lv_obj_create(tab);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 16, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t ch = 0; ch < PWM_CHANNEL_COUNT; ++ch) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_set_size(cell, 220, 120);
        lv_obj_set_style_pad_all(cell, 8, 0);
        lv_obj_set_style_pad_gap(cell, 6, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *title = lv_label_create(cell);
        lv_label_set_text_fmt(title, "Channel %u", (unsigned)ch);

        pwm_slider_ctx_t *ctx = &s_pwm_ctx[ch];
        ctx->channel = ch;
        ctx->value_label = NULL;

        lv_obj_t *slider = lv_slider_create(cell);
        lv_slider_set_range(slider, 0, 4095);
        lv_obj_set_size(slider, 180, 20);
        lv_obj_add_event_cb(slider, pwm_slider_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_add_event_cb(slider, pwm_slider_event_cb, LV_EVENT_RELEASED, ctx);
        s_pwm_slider[ch] = slider;

        lv_obj_t *value = lv_label_create(cell);
        lv_label_set_text(value, "0");
        ctx->value_label = value;
        s_pwm_label[ch] = value;
    }
}

static void create_traces_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_gap(tab, 24, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    s_chart_temp = lv_chart_create(tab);
    lv_obj_set_size(s_chart_temp, LV_PCT(100), 220);
    lv_chart_set_point_count(s_chart_temp, UI_HISTORY_POINTS);
    lv_chart_set_type(s_chart_temp, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart_temp, 5, 5);
    lv_chart_set_range(s_chart_temp, LV_CHART_AXIS_PRIMARY_Y, -400, 2000);
    s_temp_series[0] = lv_chart_add_series(s_chart_temp, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    s_temp_series[1] = lv_chart_add_series(s_chart_temp, lv_palette_main(LV_PALETTE_PINK), LV_CHART_AXIS_PRIMARY_Y);

    s_chart_hum = lv_chart_create(tab);
    lv_obj_set_size(s_chart_hum, LV_PCT(100), 220);
    lv_chart_set_point_count(s_chart_hum, UI_HISTORY_POINTS);
    lv_chart_set_type(s_chart_hum, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart_hum, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    s_hum_series[0] = lv_chart_add_series(s_chart_hum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    s_hum_series[1] = lv_chart_add_series(s_chart_hum, lv_palette_main(LV_PALETTE_TEAL), LV_CHART_AXIS_PRIMARY_Y);

    s_chart_ds = lv_chart_create(tab);
    lv_obj_set_size(s_chart_ds, LV_PCT(100), 220);
    lv_chart_set_point_count(s_chart_ds, UI_HISTORY_POINTS);
    lv_chart_set_type(s_chart_ds, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart_ds, LV_CHART_AXIS_PRIMARY_Y, -400, 2000);
    for (size_t i = 0; i < 4; ++i) {
        s_ds_series[i] = lv_chart_add_series(s_chart_ds, lv_palette_main((lv_palette_t)((i % 4) + LV_PALETTE_AMBER)),
                                             LV_CHART_AXIS_PRIMARY_Y);
    }
}

static void create_settings_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 24, 0);
    lv_obj_set_style_pad_gap(tab, 24, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *ssid_row = lv_obj_create(tab);
    lv_obj_set_size(ssid_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ssid_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ssid_row, 0, 0);
    lv_obj_set_style_pad_all(ssid_row, 0, 0);
    lv_obj_set_flex_flow(ssid_row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *ssid_label = lv_label_create(ssid_row);
    lv_label_set_text(ssid_label, "Wi-Fi SSID");
    s_ssid_ta = lv_textarea_create(ssid_row);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_obj_set_width(s_ssid_ta, 360);

    lv_obj_t *pass_row = lv_obj_create(tab);
    lv_obj_set_size(pass_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pass_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(pass_row, 0, 0);
    lv_obj_set_style_pad_all(pass_row, 0, 0);
    lv_obj_set_flex_flow(pass_row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *pass_label = lv_label_create(pass_row);
    lv_label_set_text(pass_label, "Wi-Fi Password");
    s_password_ta = lv_textarea_create(pass_row);
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_obj_set_width(s_password_ta, 360);

    lv_obj_t *mdns_row = lv_obj_create(tab);
    lv_obj_set_size(mdns_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(mdns_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(mdns_row, 0, 0);
    lv_obj_set_style_pad_all(mdns_row, 0, 0);
    lv_obj_set_flex_flow(mdns_row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *mdns_label = lv_label_create(mdns_row);
    lv_label_set_text(mdns_label, "mDNS Target Host");
    s_mdns_ta = lv_textarea_create(mdns_row);
    lv_textarea_set_one_line(s_mdns_ta, true);
    lv_obj_set_width(s_mdns_ta, 360);

    lv_obj_t *toggle_row = lv_obj_create(tab);
    lv_obj_set_style_bg_opa(toggle_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(toggle_row, 0, 0);
    lv_obj_set_style_pad_all(toggle_row, 0, 0);
    lv_obj_set_flex_flow(toggle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(toggle_row, 48, 0);

    lv_obj_t *theme_col = lv_obj_create(toggle_row);
    lv_obj_set_style_bg_opa(theme_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(theme_col, 0, 0);
    lv_obj_set_style_pad_all(theme_col, 0, 0);
    lv_obj_set_flex_flow(theme_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *theme_label = lv_label_create(theme_col);
    lv_label_set_text(theme_label, "Dark Theme");
    s_theme_switch = lv_switch_create(theme_col);

    lv_obj_t *units_col = lv_obj_create(toggle_row);
    lv_obj_set_style_bg_opa(units_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(units_col, 0, 0);
    lv_obj_set_style_pad_all(units_col, 0, 0);
    lv_obj_set_flex_flow(units_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *units_label = lv_label_create(units_col);
    lv_label_set_text(units_label, "Use °F");
    s_units_switch = lv_switch_create(units_col);

    lv_obj_t *btn_apply = lv_btn_create(tab);
    lv_obj_set_size(btn_apply, 140, 48);
    lv_obj_add_event_cb(btn_apply, prefs_apply_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn_apply);
    lv_label_set_text(btn_label, "Apply");

    lv_obj_t *btn_reset = lv_btn_create(tab);
    lv_obj_set_size(btn_reset, 140, 48);
    lv_obj_add_event_cb(btn_reset, prefs_reset_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(btn_reset);
    lv_label_set_text(reset_label, "Reset");
}

void ui_init(const ui_callbacks_t *callbacks, void *ctx)
{
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
    s_callback_ctx = ctx;
    s_updating_ui = false;
    memset(&s_active_prefs, 0, sizeof(s_active_prefs));

    apply_theme(true);

    lvgl_port_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *tabview = lv_tabview_create(scr, LV_DIR_TOP, 70);
    s_dashboard_tab = lv_tabview_add_tab(tabview, "Dashboard");
    s_gpio_tab = lv_tabview_add_tab(tabview, "GPIO");
    s_pwm_tab = lv_tabview_add_tab(tabview, "PWM");
    s_traces_tab = lv_tabview_add_tab(tabview, "Traces");
    s_settings_tab = lv_tabview_add_tab(tabview, "Settings");

    create_dashboard_tab(s_dashboard_tab);
    create_gpio_tab(s_gpio_tab);
    create_pwm_tab(s_pwm_tab);
    create_traces_tab(s_traces_tab);
    create_settings_tab(s_settings_tab);

    lvgl_port_unlock();
}

static void update_gpio_state(const proto_sensor_update_t *update)
{
    s_updating_ui = true;
    for (uint8_t dev = 0; dev < GPIO_DEVICE_COUNT; ++dev) {
        uint16_t porta = 0;
        uint16_t portb = 0;
        if (dev < 2) {
            porta = update->mcp[dev].port_a;
            portb = update->mcp[dev].port_b;
        }
        for (uint8_t pin = 0; pin < GPIO_PINS_PER_DEVICE; ++pin) {
            bool on = false;
            if (pin < 8) {
                on = ((porta >> pin) & 0x1U) != 0U;
            } else {
                on = ((portb >> (pin - 8U)) & 0x1U) != 0U;
            }
            lv_obj_t *sw = s_gpio_switch[dev][pin];
            if (sw) {
                if (on) {
                    lv_obj_add_state(sw, LV_STATE_CHECKED);
                    lv_label_set_text(s_gpio_state_label[dev][pin], "ON");
                } else {
                    lv_obj_clear_state(sw, LV_STATE_CHECKED);
                    lv_label_set_text(s_gpio_state_label[dev][pin], "OFF");
                }
            }
        }
    }
    s_updating_ui = false;
}

static void update_pwm_state(const proto_sensor_update_t *update)
{
    s_updating_ui = true;
    lv_slider_set_value(s_pwm_freq_slider, update->pwm.frequency_hz, LV_ANIM_OFF);
    char freq_text[16];
    snprintf(freq_text, sizeof(freq_text), "%u Hz", update->pwm.frequency_hz);
    lv_label_set_text(s_pwm_freq_label, freq_text);

    for (uint8_t ch = 0; ch < PWM_CHANNEL_COUNT; ++ch) {
        uint16_t duty = update->pwm.duty_cycle[ch];
        if (s_pwm_slider[ch]) {
            lv_slider_set_value(s_pwm_slider[ch], duty, LV_ANIM_OFF);
        }
        if (s_pwm_label[ch]) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", duty);
            lv_label_set_text(s_pwm_label[ch], buf);
        }
    }
    s_updating_ui = false;
}

static void update_charts(const proto_sensor_update_t *update, bool use_fahrenheit)
{
    for (size_t i = 0; i < 2; ++i) {
        bool valid = i < update->sht20_count && update->sht20[i].valid;
        float temp = valid ? update->sht20[i].temperature_c : NAN;
        float hum = valid ? update->sht20[i].humidity_percent : NAN;
        if (valid && isfinite(temp)) {
            float value = use_fahrenheit ? (temp * 9.0f / 5.0f + 32.0f) : temp;
            lv_chart_set_next_value(s_chart_temp, s_temp_series[i], (lv_coord_t)(value * 10.0f));
        } else {
            lv_chart_set_next_value(s_chart_temp, s_temp_series[i], LV_CHART_POINT_NONE);
        }
        if (valid && isfinite(hum)) {
            lv_chart_set_next_value(s_chart_hum, s_hum_series[i], (lv_coord_t)(hum * 10.0f));
        } else {
            lv_chart_set_next_value(s_chart_hum, s_hum_series[i], LV_CHART_POINT_NONE);
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        float temp = i < update->ds18b20_count ? update->ds18b20[i].temperature_c : NAN;
        if (!isnan(temp)) {
            float value = use_fahrenheit ? (temp * 9.0f / 5.0f + 32.0f) : temp;
            lv_chart_set_next_value(s_chart_ds, s_ds_series[i], (lv_coord_t)(value * 10.0f));
        }
    }
}

void ui_update_sensor_data(const proto_sensor_update_t *update, bool use_fahrenheit)
{
    if (!update) {
        return;
    }
    char buf[64];
    for (size_t i = 0; i < 2; ++i) {
        if (i < update->sht20_count) {
            const proto_sht20_reading_t *reading = &update->sht20[i];
            const char *suffix = reading->valid ? "" : " (fault)";
            lv_label_set_text_fmt(s_sht20_name[i], "%s%s", reading->id, suffix);
            if (reading->valid && isfinite(reading->temperature_c)) {
                lv_label_set_text_fmt(s_sht20_temp[i], "Temp: %s",
                                      format_temperature(buf, sizeof(buf), reading->temperature_c, use_fahrenheit));
            } else {
                lv_label_set_text(s_sht20_temp[i], "Temp: --");
            }
            if (reading->valid && isfinite(reading->humidity_percent)) {
                snprintf(buf, sizeof(buf), "RH: %.1f %%", reading->humidity_percent);
                lv_label_set_text(s_sht20_hum[i], buf);
            } else {
                lv_label_set_text(s_sht20_hum[i], "RH: --");
            }
        } else {
            lv_label_set_text_fmt(s_sht20_name[i], "Sensor %u", (unsigned)(i + 1));
            lv_label_set_text(s_sht20_temp[i], "Temp: --");
            lv_label_set_text(s_sht20_hum[i], "RH: --");
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        if (i < update->ds18b20_count) {
            char rom[17];
            format_rom_code(update->ds18b20[i].rom_code, rom, sizeof(rom));
            lv_label_set_text_fmt(s_ds18_name[i], "%s", rom);
            lv_label_set_text_fmt(s_ds18_temp[i], "Temp: %s",
                                  format_temperature(buf, sizeof(buf), update->ds18b20[i].temperature_c,
                                                     use_fahrenheit));
        } else {
            lv_label_set_text(s_ds18_temp[i], "Temp: --");
        }
    }
    if (s_wifi_badge) {
        lv_label_set_text_fmt(s_wifi_badge, "Seq: %u", update->sequence_id);
    }
    update_gpio_state(update);
    update_pwm_state(update);
    update_charts(update, use_fahrenheit);
}

void ui_update_connection_status(bool connected)
{
    if (!s_status_label) {
        return;
    }
    if (connected) {
        lv_label_set_text(s_status_label, "Wi-Fi: Connected");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x66FF66), 0);
    } else {
        lv_label_set_text(s_status_label, "Wi-Fi: Disconnected");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFF5555), 0);
    }
}

void ui_update_crc_status(bool crc_ok)
{
    if (!s_crc_badge) {
        return;
    }
    if (crc_ok) {
        lv_label_set_text(s_crc_badge, "CRC: OK");
        lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0x66FFAA), 0);
    } else {
        lv_label_set_text(s_crc_badge, "CRC: ERROR");
        lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0xFF6666), 0);
    }
}

void ui_apply_preferences(const hmi_user_preferences_t *prefs)
{
    if (!prefs) {
        return;
    }
    s_active_prefs = *prefs;
    apply_theme(prefs->dark_theme);
    s_updating_ui = true;
    lv_textarea_set_text(s_ssid_ta, prefs->ssid);
    lv_textarea_set_text(s_password_ta, prefs->password);
    lv_textarea_set_text(s_mdns_ta, prefs->mdns_target);
    if (prefs->dark_theme) {
        lv_obj_add_state(s_theme_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_theme_switch, LV_STATE_CHECKED);
    }
    if (prefs->use_fahrenheit) {
        lv_obj_add_state(s_units_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_units_switch, LV_STATE_CHECKED);
    }
    s_updating_ui = false;
}

void ui_process(void)
{
    lvgl_port_flush_ready();
}

#if CONFIG_BUILD_UNIT_TESTS
void ui_set_callbacks_for_test(const ui_callbacks_t *callbacks, void *ctx)
{
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
    s_callback_ctx = ctx;
}

void ui_set_active_prefs_for_test(const hmi_user_preferences_t *prefs)
{
    if (prefs) {
        s_active_prefs = *prefs;
    } else {
        memset(&s_active_prefs, 0, sizeof(s_active_prefs));
    }
}

void ui_test_handle_gpio_switch(uint8_t device_index, uint8_t pin_index, bool on)
{
    gpio_switch_ctx_t ctx = {
        .device_index = device_index,
        .pin_index = pin_index,
    };
    dispatch_gpio_write(&ctx, on);
}

void ui_test_handle_pwm_slider(uint8_t channel, uint16_t value)
{
    pwm_slider_ctx_t ctx = {
        .channel = channel,
        .value_label = NULL,
    };
    dispatch_pwm_value(&ctx, (int16_t)value);
}

void ui_test_handle_pwm_frequency(uint16_t freq)
{
    dispatch_pwm_frequency(freq);
}

void ui_test_apply_preferences_inputs(const char *ssid, const char *password, const char *mdns, bool dark,
                                      bool fahrenheit)
{
    dispatch_apply_preferences(ssid, password, mdns, dark, fahrenheit);
}

void ui_test_trigger_reset_preferences(void)
{
    dispatch_reset_preferences();
}
#endif
