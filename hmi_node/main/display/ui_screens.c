#include "display/ui_screens.h"

#include "display/lvgl_port.h"
#include "display/ui_locale.h"
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

static lv_obj_t *s_tabview;
static lv_obj_t *s_status_label;
static lv_obj_t *s_crc_badge;
static lv_obj_t *s_wifi_badge;
static lv_obj_t *s_dashboard_tab;
static lv_obj_t *s_gpio_tab;
static lv_obj_t *s_pwm_tab;
static lv_obj_t *s_traces_tab;
static lv_obj_t *s_settings_tab;
static lv_obj_t *s_accessibility_tab;

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
static lv_obj_t *s_language_dd;
static lv_obj_t *s_high_contrast_switch;
static lv_obj_t *s_text_scale_slider;
static lv_obj_t *s_text_scale_label;
static lv_obj_t *s_touch_targets_switch;
static lv_obj_t *s_ssid_label;
static lv_obj_t *s_password_label;
static lv_obj_t *s_mdns_label;
static lv_obj_t *s_dark_theme_label;
static lv_obj_t *s_use_fahrenheit_label;
static lv_obj_t *s_apply_label;
static lv_obj_t *s_reset_label;
static lv_obj_t *s_language_label;
static lv_obj_t *s_high_contrast_label;
static lv_obj_t *s_text_scale_title;
static lv_obj_t *s_touch_targets_label;
static lv_obj_t *s_apply_btn;
static lv_obj_t *s_reset_btn;
static proto_sensor_update_t s_last_proto_update;
static bool s_has_last_proto_update;
static bool s_last_connected;
static bool s_last_crc_ok;
static uint32_t s_last_sequence_id;

static void refresh_localised_text(void);
static void apply_touch_target_style(void);
static void update_text_scale_label(void);
static const ui_locale_pack_t *get_locale(void);

static void apply_theme(bool dark)
{
    lvgl_port_lock();
    lv_disp_t *disp = lv_disp_get_default();
    lv_color_t primary = s_active_prefs.high_contrast ? lv_color_hex(0x000000) : lv_palette_main(LV_PALETTE_BLUE);
    lv_color_t secondary = s_active_prefs.high_contrast ? lv_color_hex(0xFFD54F) : lv_palette_darken(LV_PALETTE_GREY, 2);
    lv_theme_t *theme = lv_theme_default_init(disp, primary, secondary, dark, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, theme);
    apply_display_zoom();
    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        if (s_active_prefs.high_contrast) {
            lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        } else {
            lv_obj_set_style_bg_color(scr, lv_color_hex(dark ? 0x101418 : 0xF5F7FA), 0);
        }
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    }
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

static void anim_translate_exec(void *var, int32_t v)
{
    if (!var) {
        return;
    }
    lv_obj_set_style_translate_y((lv_obj_t *)var, (lv_coord_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void anim_fade_exec(void *var, int32_t v)
{
    if (!var) {
        return;
    }
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void animate_entrance(lv_obj_t *obj, uint32_t delay_ms)
{
    if (!obj) {
        return;
    }
    lv_obj_set_style_translate_y(obj, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_anim_t slide;
    lv_anim_init(&slide);
    lv_anim_set_var(&slide, obj);
    lv_anim_set_values(&slide, 48, 0);
    lv_anim_set_time(&slide, 320);
    lv_anim_set_delay(&slide, delay_ms);
    lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&slide, anim_translate_exec);
    lv_anim_start(&slide);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&fade, 260);
    lv_anim_set_delay(&fade, delay_ms + 60);
    lv_anim_set_exec_cb(&fade, anim_fade_exec);
    lv_anim_start(&fade);
}

static const ui_locale_pack_t *get_locale(void)
{
    hmi_language_t lang = s_active_prefs.language;
    if (lang >= HMI_LANGUAGE_MAX) {
        lang = HMI_LANGUAGE_EN;
    }
    return ui_locale_get_pack(lang);
}

static void apply_display_zoom(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) {
        return;
    }
    uint32_t percent = s_active_prefs.text_scale_percent ? s_active_prefs.text_scale_percent : 100U;
    if (percent < 80U) {
        percent = 80U;
    }
    if (percent > 140U) {
        percent = 140U;
    }
    uint16_t zoom = (uint16_t)((percent * 256U) / 100U);
    lv_disp_set_zoom(disp, zoom);
}

static void update_text_scale_label(void)
{
    if (!s_text_scale_label || !s_text_scale_slider) {
        return;
    }
    char buf[16];
    uint16_t value = (uint16_t)lv_slider_get_value(s_text_scale_slider);
    lv_snprintf(buf, sizeof(buf), "%u%%", (unsigned)value);
    lv_label_set_text(s_text_scale_label, buf);
}

static void apply_touch_target_style(void)
{
    lv_coord_t pad = s_active_prefs.large_touch_targets ? 12 : 4;
    lv_coord_t btn_height = s_active_prefs.large_touch_targets ? 60 : 48;
    lv_coord_t btn_width = s_active_prefs.large_touch_targets ? 170 : 140;

    for (size_t dev = 0; dev < GPIO_DEVICE_COUNT; ++dev) {
        for (size_t pin = 0; pin < GPIO_PINS_PER_DEVICE; ++pin) {
            lv_obj_t *sw = s_gpio_switch[dev][pin];
            if (sw) {
                lv_obj_set_style_pad_all(sw, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }

    if (s_theme_switch) {
        lv_obj_set_style_pad_all(s_theme_switch, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_units_switch) {
        lv_obj_set_style_pad_all(s_units_switch, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_high_contrast_switch) {
        lv_obj_set_style_pad_all(s_high_contrast_switch, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_touch_targets_switch) {
        lv_obj_set_style_pad_all(s_touch_targets_switch, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_apply_btn) {
        lv_obj_set_size(s_apply_btn, btn_width, btn_height);
    }
    if (s_reset_btn) {
        lv_obj_set_size(s_reset_btn, btn_width, btn_height);
    }
    if (s_text_scale_slider) {
        lv_obj_set_style_pad_all(s_text_scale_slider, pad, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(s_text_scale_slider, pad, LV_PART_KNOB | LV_STATE_DEFAULT);
    }
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
                                       bool fahrenheit, bool high_contrast, bool large_touch_targets,
                                       uint8_t text_scale_percent, hmi_language_t language)
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
    prefs.high_contrast = high_contrast;
    prefs.large_touch_targets = large_touch_targets;
    if (text_scale_percent < 80U || text_scale_percent > 140U) {
        text_scale_percent = 100U;
    }
    prefs.text_scale_percent = text_scale_percent;
    prefs.language = (language < HMI_LANGUAGE_MAX) ? language : HMI_LANGUAGE_EN;
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

static void text_scale_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    update_text_scale_label();
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
    bool high_contrast = s_high_contrast_switch && lv_obj_has_state(s_high_contrast_switch, LV_STATE_CHECKED);
    bool touch_targets = s_touch_targets_switch && lv_obj_has_state(s_touch_targets_switch, LV_STATE_CHECKED);
    uint8_t text_scale = s_text_scale_slider ? (uint8_t)lv_slider_get_value(s_text_scale_slider)
                                             : (s_active_prefs.text_scale_percent ? s_active_prefs.text_scale_percent : 100U);
    uint16_t selected = s_language_dd ? lv_dropdown_get_selected(s_language_dd) : (uint16_t)s_active_prefs.language;
    hmi_language_t language = (selected < HMI_LANGUAGE_MAX) ? (hmi_language_t)selected : HMI_LANGUAGE_EN;
    dispatch_apply_preferences(ssid, password, mdns, dark, fahrenheit, high_contrast, touch_targets, text_scale, language);
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
    const ui_locale_pack_t *locale = get_locale();
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
    lv_label_set_text(s_status_label, locale->label_wifi_disconnected);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFF5555), 0);

    s_wifi_badge = lv_label_create(status_row);
    lv_label_set_text_fmt(s_wifi_badge, "%s: --", locale->label_sequence_prefix);
    lv_obj_set_style_text_color(s_wifi_badge, lv_color_hex(0xAAAAAA), 0);

    s_crc_badge = lv_label_create(status_row);
    lv_label_set_text(s_crc_badge, locale->label_crc_unknown);
    lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0xAAAAAA), 0);
    animate_entrance(status_row, 0);

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
        lv_label_set_text_fmt(s_sht20_name[i], "%s %u", locale->label_sensor_fallback, (unsigned)(i + 1));
        s_sht20_temp[i] = lv_label_create(card);
        lv_label_set_text_fmt(s_sht20_temp[i], "%s: --", locale->label_temperature_prefix);
        s_sht20_hum[i] = lv_label_create(card);
        lv_label_set_text_fmt(s_sht20_hum[i], "%s: --", locale->label_humidity_prefix);
        animate_entrance(card, (uint32_t)(i * 60));
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
        lv_label_set_text_fmt(s_ds18_temp[i], "%s: --", locale->label_temperature_prefix);
        animate_entrance(card, (uint32_t)(i * 50 + 120));
    }
}

static void create_gpio_tab(lv_obj_t *tab)
{
    const ui_locale_pack_t *locale = get_locale();
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

        animate_entrance(panel, (uint32_t)(dev * 80));

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
            lv_label_set_text(state, locale->label_gpio_off);
            s_gpio_state_label[dev][pin] = state;
        }
    }
}

static void create_pwm_tab(lv_obj_t *tab)
{
    const ui_locale_pack_t *locale = get_locale();
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
    lv_label_set_text(freq_label, locale->label_frequency);

    s_pwm_freq_slider = lv_slider_create(freq_row);
    lv_slider_set_range(s_pwm_freq_slider, 40, 2000);
    lv_obj_set_size(s_pwm_freq_slider, 300, 20);
    lv_obj_add_event_cb(s_pwm_freq_slider, pwm_freq_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_pwm_freq_slider, pwm_freq_event_cb, LV_EVENT_RELEASED, NULL);

    s_pwm_freq_label = lv_label_create(freq_row);
    lv_label_set_text(s_pwm_freq_label, "500 Hz");
    animate_entrance(freq_row, 40);

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
        lv_label_set_text_fmt(title, "%s %u", locale->label_channel_prefix, (unsigned)ch);

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
        animate_entrance(cell, (uint32_t)(ch * 15 + 80));
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
    animate_entrance(s_chart_temp, 40);

    s_chart_hum = lv_chart_create(tab);
    lv_obj_set_size(s_chart_hum, LV_PCT(100), 220);
    lv_chart_set_point_count(s_chart_hum, UI_HISTORY_POINTS);
    lv_chart_set_type(s_chart_hum, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart_hum, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    s_hum_series[0] = lv_chart_add_series(s_chart_hum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    s_hum_series[1] = lv_chart_add_series(s_chart_hum, lv_palette_main(LV_PALETTE_TEAL), LV_CHART_AXIS_PRIMARY_Y);
    animate_entrance(s_chart_hum, 80);

    s_chart_ds = lv_chart_create(tab);
    lv_obj_set_size(s_chart_ds, LV_PCT(100), 220);
    lv_chart_set_point_count(s_chart_ds, UI_HISTORY_POINTS);
    lv_chart_set_type(s_chart_ds, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart_ds, LV_CHART_AXIS_PRIMARY_Y, -400, 2000);
    for (size_t i = 0; i < 4; ++i) {
        s_ds_series[i] = lv_chart_add_series(s_chart_ds, lv_palette_main((lv_palette_t)((i % 4) + LV_PALETTE_AMBER)),
                                             LV_CHART_AXIS_PRIMARY_Y);
    }
    animate_entrance(s_chart_ds, 120);
}

static void create_settings_tab(lv_obj_t *tab)
{
    const ui_locale_pack_t *locale = get_locale();
    lv_obj_set_style_pad_all(tab, 24, 0);
    lv_obj_set_style_pad_gap(tab, 24, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *ssid_row = lv_obj_create(tab);
    lv_obj_set_size(ssid_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ssid_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ssid_row, 0, 0);
    lv_obj_set_style_pad_all(ssid_row, 0, 0);
    lv_obj_set_flex_flow(ssid_row, LV_FLEX_FLOW_COLUMN);

    s_ssid_label = lv_label_create(ssid_row);
    lv_label_set_text(s_ssid_label, locale->label_wifi_ssid);
    s_ssid_ta = lv_textarea_create(ssid_row);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_obj_set_width(s_ssid_ta, 360);

    lv_obj_t *pass_row = lv_obj_create(tab);
    lv_obj_set_size(pass_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pass_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(pass_row, 0, 0);
    lv_obj_set_style_pad_all(pass_row, 0, 0);
    lv_obj_set_flex_flow(pass_row, LV_FLEX_FLOW_COLUMN);

    s_password_label = lv_label_create(pass_row);
    lv_label_set_text(s_password_label, locale->label_wifi_password);
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

    s_mdns_label = lv_label_create(mdns_row);
    lv_label_set_text(s_mdns_label, locale->label_mdns_target);
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
    s_dark_theme_label = lv_label_create(theme_col);
    lv_label_set_text(s_dark_theme_label, locale->label_dark_theme);
    s_theme_switch = lv_switch_create(theme_col);

    lv_obj_t *units_col = lv_obj_create(toggle_row);
    lv_obj_set_style_bg_opa(units_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(units_col, 0, 0);
    lv_obj_set_style_pad_all(units_col, 0, 0);
    lv_obj_set_flex_flow(units_col, LV_FLEX_FLOW_COLUMN);
    s_use_fahrenheit_label = lv_label_create(units_col);
    lv_label_set_text(s_use_fahrenheit_label, locale->label_use_fahrenheit);
    s_units_switch = lv_switch_create(units_col);

    s_apply_btn = lv_btn_create(tab);
    lv_obj_set_size(s_apply_btn, 140, 48);
    lv_obj_add_event_cb(s_apply_btn, prefs_apply_event_cb, LV_EVENT_CLICKED, NULL);
    s_apply_label = lv_label_create(s_apply_btn);
    lv_label_set_text(s_apply_label, locale->label_apply);

    s_reset_btn = lv_btn_create(tab);
    lv_obj_set_size(s_reset_btn, 140, 48);
    lv_obj_add_event_cb(s_reset_btn, prefs_reset_event_cb, LV_EVENT_CLICKED, NULL);
    s_reset_label = lv_label_create(s_reset_btn);
    lv_label_set_text(s_reset_label, locale->label_reset);

    animate_entrance(ssid_row, 40);
    animate_entrance(pass_row, 80);
    animate_entrance(mdns_row, 120);
    animate_entrance(toggle_row, 160);
    animate_entrance(s_apply_btn, 200);
    animate_entrance(s_reset_btn, 240);
}

static void create_accessibility_tab(lv_obj_t *tab)
{
    const ui_locale_pack_t *locale = get_locale();
    lv_obj_set_style_pad_all(tab, 24, 0);
    lv_obj_set_style_pad_gap(tab, 28, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lang_row = lv_obj_create(tab);
    lv_obj_set_size(lang_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lang_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(lang_row, 0, 0);
    lv_obj_set_style_pad_all(lang_row, 0, 0);
    lv_obj_set_flex_flow(lang_row, LV_FLEX_FLOW_COLUMN);

    s_language_label = lv_label_create(lang_row);
    lv_label_set_text(s_language_label, locale->label_language);
    s_language_dd = lv_dropdown_create(lang_row);
    lv_dropdown_set_options(s_language_dd, locale->dropdown_languages);
    lv_obj_set_width(s_language_dd, 220);

    lv_obj_t *contrast_row = lv_obj_create(tab);
    lv_obj_set_size(contrast_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(contrast_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(contrast_row, 0, 0);
    lv_obj_set_style_pad_all(contrast_row, 0, 0);
    lv_obj_set_flex_flow(contrast_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(contrast_row, 16, 0);

    s_high_contrast_label = lv_label_create(contrast_row);
    lv_label_set_text(s_high_contrast_label, locale->label_high_contrast);
    s_high_contrast_switch = lv_switch_create(contrast_row);

    lv_obj_t *scale_row = lv_obj_create(tab);
    lv_obj_set_size(scale_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scale_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(scale_row, 0, 0);
    lv_obj_set_style_pad_all(scale_row, 0, 0);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(scale_row, 8, 0);

    s_text_scale_title = lv_label_create(scale_row);
    lv_label_set_text(s_text_scale_title, locale->label_text_scale);
    s_text_scale_slider = lv_slider_create(scale_row);
    lv_obj_set_width(s_text_scale_slider, 360);
    lv_slider_set_range(s_text_scale_slider, 80, 140);
    lv_slider_set_value(s_text_scale_slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_text_scale_slider, text_scale_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_text_scale_label = lv_label_create(scale_row);
    update_text_scale_label();

    lv_obj_t *touch_row = lv_obj_create(tab);
    lv_obj_set_size(touch_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(touch_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(touch_row, 0, 0);
    lv_obj_set_style_pad_all(touch_row, 0, 0);
    lv_obj_set_flex_flow(touch_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(touch_row, 16, 0);

    s_touch_targets_label = lv_label_create(touch_row);
    lv_label_set_text(s_touch_targets_label, locale->label_touch_targets);
    s_touch_targets_switch = lv_switch_create(touch_row);

    animate_entrance(lang_row, 40);
    animate_entrance(contrast_row, 80);
    animate_entrance(scale_row, 120);
    animate_entrance(touch_row, 160);
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
    s_active_prefs.text_scale_percent = 100;
    s_active_prefs.language = HMI_LANGUAGE_EN;
    s_active_prefs.dark_theme = true;
    s_last_connected = false;
    s_last_crc_ok = true;
    s_last_sequence_id = 0;
    s_has_last_proto_update = false;

    apply_theme(true);

    lvgl_port_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tabview = lv_tabview_create(scr, LV_DIR_TOP, 70);
    s_dashboard_tab = lv_tabview_add_tab(s_tabview, "Dashboard");
    s_gpio_tab = lv_tabview_add_tab(s_tabview, "GPIO");
    s_pwm_tab = lv_tabview_add_tab(s_tabview, "PWM");
    s_traces_tab = lv_tabview_add_tab(s_tabview, "Traces");
    s_settings_tab = lv_tabview_add_tab(s_tabview, "Settings");
    s_accessibility_tab = lv_tabview_add_tab(s_tabview, "Accessibility");

    create_dashboard_tab(s_dashboard_tab);
    create_gpio_tab(s_gpio_tab);
    create_pwm_tab(s_pwm_tab);
    create_traces_tab(s_traces_tab);
    create_settings_tab(s_settings_tab);
    create_accessibility_tab(s_accessibility_tab);
    refresh_localised_text();

    lvgl_port_unlock();
}

static void update_gpio_state(const proto_sensor_update_t *update)
{
    s_updating_ui = true;
    const ui_locale_pack_t *locale = get_locale();
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
                    lv_label_set_text(s_gpio_state_label[dev][pin], locale->label_gpio_on);
                } else {
                    lv_obj_clear_state(sw, LV_STATE_CHECKED);
                    lv_label_set_text(s_gpio_state_label[dev][pin], locale->label_gpio_off);
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

static void refresh_localised_text(void)
{
    const ui_locale_pack_t *locale = get_locale();
    if (!locale) {
        return;
    }
    if (s_tabview) {
        if (s_dashboard_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_dashboard_tab), locale->tab_dashboard);
        }
        if (s_gpio_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_gpio_tab), locale->tab_gpio);
        }
        if (s_pwm_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_pwm_tab), locale->tab_pwm);
        }
        if (s_traces_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_traces_tab), locale->tab_traces);
        }
        if (s_settings_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_settings_tab), locale->tab_settings);
        }
        if (s_accessibility_tab) {
            lv_tabview_set_tab_title(s_tabview, lv_obj_get_index(s_accessibility_tab), locale->tab_accessibility);
        }
    }

    if (s_ssid_label) {
        lv_label_set_text(s_ssid_label, locale->label_wifi_ssid);
    }
    if (s_password_label) {
        lv_label_set_text(s_password_label, locale->label_wifi_password);
    }
    if (s_mdns_label) {
        lv_label_set_text(s_mdns_label, locale->label_mdns_target);
    }
    if (s_dark_theme_label) {
        lv_label_set_text(s_dark_theme_label, locale->label_dark_theme);
    }
    if (s_use_fahrenheit_label) {
        lv_label_set_text(s_use_fahrenheit_label, locale->label_use_fahrenheit);
    }
    if (s_apply_label) {
        lv_label_set_text(s_apply_label, locale->label_apply);
    }
    if (s_reset_label) {
        lv_label_set_text(s_reset_label, locale->label_reset);
    }
    if (s_language_label) {
        lv_label_set_text(s_language_label, locale->label_language);
    }
    if (s_high_contrast_label) {
        lv_label_set_text(s_high_contrast_label, locale->label_high_contrast);
    }
    if (s_text_scale_title) {
        lv_label_set_text(s_text_scale_title, locale->label_text_scale);
    }
    if (s_touch_targets_label) {
        lv_label_set_text(s_touch_targets_label, locale->label_touch_targets);
    }
    if (s_language_dd) {
        lv_dropdown_set_options(s_language_dd, locale->dropdown_languages);
    }

    if (s_wifi_badge) {
        if (s_last_sequence_id != 0U) {
            lv_label_set_text_fmt(s_wifi_badge, "%s: %u", locale->label_sequence_prefix, (unsigned)s_last_sequence_id);
        } else {
            lv_label_set_text_fmt(s_wifi_badge, "%s: --", locale->label_sequence_prefix);
        }
    }
    update_text_scale_label();

    if (s_status_label) {
        ui_update_connection_status(s_last_connected);
    }
    if (s_crc_badge) {
        if (s_has_last_proto_update) {
            ui_update_crc_status(s_last_crc_ok);
        } else {
            lv_label_set_text(s_crc_badge, locale->label_crc_unknown);
            lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0xAAAAAA), 0);
        }
    }

    if (s_has_last_proto_update) {
        ui_update_sensor_data(&s_last_proto_update, s_active_prefs.use_fahrenheit);
    } else {
        for (size_t i = 0; i < 2; ++i) {
            if (s_sht20_name[i]) {
                lv_label_set_text_fmt(s_sht20_name[i], "%s %u", locale->label_sensor_fallback, (unsigned)(i + 1));
            }
            if (s_sht20_temp[i]) {
                lv_label_set_text_fmt(s_sht20_temp[i], "%s: --", locale->label_temperature_prefix);
            }
            if (s_sht20_hum[i]) {
                lv_label_set_text_fmt(s_sht20_hum[i], "%s: --", locale->label_humidity_prefix);
            }
        }
        for (size_t i = 0; i < 4; ++i) {
            if (s_ds18_temp[i]) {
                lv_label_set_text_fmt(s_ds18_temp[i], "%s: --", locale->label_temperature_prefix);
            }
        }
    }
}

void ui_update_sensor_data(const proto_sensor_update_t *update, bool use_fahrenheit)
{
    if (!update) {
        return;
    }
    const ui_locale_pack_t *locale = get_locale();
    char buf[64];
    for (size_t i = 0; i < 2; ++i) {
        if (i < update->sht20_count) {
            const proto_sht20_reading_t *reading = &update->sht20[i];
            const char *suffix = reading->valid ? "" : " (fault)";
            lv_label_set_text_fmt(s_sht20_name[i], "%s%s", reading->id, suffix);
            if (reading->valid && isfinite(reading->temperature_c)) {
                lv_label_set_text_fmt(s_sht20_temp[i], "%s: %s", locale->label_temperature_prefix,
                                      format_temperature(buf, sizeof(buf), reading->temperature_c, use_fahrenheit));
            } else {
                lv_label_set_text_fmt(s_sht20_temp[i], "%s: --", locale->label_temperature_prefix);
            }
            if (reading->valid && isfinite(reading->humidity_percent)) {
                snprintf(buf, sizeof(buf), "%.1f %%", reading->humidity_percent);
                lv_label_set_text_fmt(s_sht20_hum[i], "%s: %s", locale->label_humidity_prefix, buf);
            } else {
                lv_label_set_text_fmt(s_sht20_hum[i], "%s: --", locale->label_humidity_prefix);
            }
        } else {
            lv_label_set_text_fmt(s_sht20_name[i], "%s %u", locale->label_sensor_fallback, (unsigned)(i + 1));
            lv_label_set_text_fmt(s_sht20_temp[i], "%s: --", locale->label_temperature_prefix);
            lv_label_set_text_fmt(s_sht20_hum[i], "%s: --", locale->label_humidity_prefix);
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        if (i < update->ds18b20_count) {
            char rom[17];
            format_rom_code(update->ds18b20[i].rom_code, rom, sizeof(rom));
            lv_label_set_text_fmt(s_ds18_name[i], "%s", rom);
            lv_label_set_text_fmt(s_ds18_temp[i], "%s: %s", locale->label_temperature_prefix,
                                  format_temperature(buf, sizeof(buf), update->ds18b20[i].temperature_c, use_fahrenheit));
        } else {
            lv_label_set_text_fmt(s_ds18_temp[i], "%s: --", locale->label_temperature_prefix);
        }
    }
    if (s_wifi_badge) {
        lv_label_set_text_fmt(s_wifi_badge, "%s: %u", locale->label_sequence_prefix, update->sequence_id);
    }
    update_gpio_state(update);
    update_pwm_state(update);
    update_charts(update, use_fahrenheit);
    s_last_sequence_id = update->sequence_id;
    s_last_proto_update = *update;
    s_has_last_proto_update = true;
}

void ui_update_connection_status(bool connected)
{
    if (!s_status_label) {
        return;
    }
    s_last_connected = connected;
    const ui_locale_pack_t *locale = get_locale();
    if (connected) {
        lv_label_set_text(s_status_label, locale->label_wifi_connected);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x66FF66), 0);
    } else {
        lv_label_set_text(s_status_label, locale->label_wifi_disconnected);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFF5555), 0);
    }
}

void ui_update_crc_status(bool crc_ok)
{
    if (!s_crc_badge) {
        return;
    }
    s_last_crc_ok = crc_ok;
    const ui_locale_pack_t *locale = get_locale();
    if (crc_ok) {
        lv_label_set_text(s_crc_badge, locale->label_crc_ok);
        lv_obj_set_style_text_color(s_crc_badge, lv_color_hex(0x66FFAA), 0);
    } else {
        lv_label_set_text(s_crc_badge, locale->label_crc_error);
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
    if (s_high_contrast_switch) {
        if (prefs->high_contrast) {
            lv_obj_add_state(s_high_contrast_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_high_contrast_switch, LV_STATE_CHECKED);
        }
    }
    if (s_touch_targets_switch) {
        if (prefs->large_touch_targets) {
            lv_obj_add_state(s_touch_targets_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_touch_targets_switch, LV_STATE_CHECKED);
        }
    }
    if (s_text_scale_slider) {
        lv_slider_set_value(s_text_scale_slider, prefs->text_scale_percent, LV_ANIM_OFF);
        update_text_scale_label();
    }
    if (s_language_dd) {
        lv_dropdown_set_selected(s_language_dd, prefs->language < HMI_LANGUAGE_MAX ? prefs->language : HMI_LANGUAGE_EN);
    }
    s_updating_ui = false;
    apply_touch_target_style();
    refresh_localised_text();
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
                                      bool fahrenheit, bool high_contrast, bool large_touch_targets,
                                      uint8_t text_scale_percent, hmi_language_t language)
{
    dispatch_apply_preferences(ssid, password, mdns, dark, fahrenheit, high_contrast, large_touch_targets,
                               text_scale_percent, language);
}

void ui_test_trigger_reset_preferences(void)
{
    dispatch_reset_preferences();
}
#endif
