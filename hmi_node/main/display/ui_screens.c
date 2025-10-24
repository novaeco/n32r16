#include "display/ui_screens.h"

#include "display/lvgl_port.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *s_status_label;
static lv_obj_t *s_sht20_labels[2];
static lv_obj_t *s_ds18_labels[4];

void ui_init(void)
{
    lvgl_port_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);

    s_status_label = lv_label_create(scr);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_text_color(s_status_label, lv_color_white(), 0);
    lv_label_set_text(s_status_label, "Wi-Fi: --");

    for (size_t i = 0; i < 2; ++i) {
        s_sht20_labels[i] = lv_label_create(scr);
        lv_obj_align(s_sht20_labels[i], LV_ALIGN_TOP_LEFT, 10, 60 + i * 30);
        lv_obj_set_style_text_color(s_sht20_labels[i], lv_color_hex(0x33CCFF), 0);
        lv_label_set_text_fmt(s_sht20_labels[i], "SHT20_%u: --", (unsigned)i + 1);
    }
    for (size_t i = 0; i < 4; ++i) {
        s_ds18_labels[i] = lv_label_create(scr);
        lv_obj_align(s_ds18_labels[i], LV_ALIGN_TOP_RIGHT, -10, 60 + i * 30);
        lv_obj_set_style_text_color(s_ds18_labels[i], lv_color_hex(0xFF9933), 0);
        lv_label_set_text_fmt(s_ds18_labels[i], "DS18B20_%u: --", (unsigned)i + 1);
    }
    lvgl_port_unlock();
}

void ui_update_sensor_data(const proto_sensor_update_t *update)
{
    if (!update) {
        return;
    }
    lvgl_port_lock();
    for (size_t i = 0; i < update->sht20_count && i < 2; ++i) {
        lv_label_set_text_fmt(s_sht20_labels[i], "%s: %.2f °C / %.1f %%RH", update->sht20[i].id,
                              update->sht20[i].temperature_c, update->sht20[i].humidity_percent);
    }
    for (size_t i = update->sht20_count; i < 2; ++i) {
        lv_label_set_text_fmt(s_sht20_labels[i], "SHT20_%u: --", (unsigned)i + 1);
    }
    for (size_t i = 0; i < update->ds18b20_count && i < 4; ++i) {
        lv_label_set_text_fmt(s_ds18_labels[i], "DS%zu: %.2f °C", i + 1, update->ds18b20[i].temperature_c);
    }
    for (size_t i = update->ds18b20_count; i < 4; ++i) {
        lv_label_set_text_fmt(s_ds18_labels[i], "DS18B20_%u: --", (unsigned)i + 1);
    }
    lvgl_port_unlock();
}

void ui_update_connection_status(bool connected)
{
    lvgl_port_lock();
    lv_label_set_text_fmt(s_status_label, "Wi-Fi: %s", connected ? "Connected" : "Disconnected");
    lv_obj_set_style_text_color(s_status_label, connected ? lv_color_hex(0x44FF44) : lv_color_hex(0xFF4444), 0);
    lvgl_port_unlock();
}

void ui_process(void)
{
    lvgl_port_flush_ready();
}
