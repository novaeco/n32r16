#include "ui_locale.h"

static const ui_locale_pack_t s_pack_en = {
    .tab_dashboard = "Dashboard",
    .tab_gpio = "GPIO",
    .tab_pwm = "PWM",
    .tab_traces = "Traces",
    .tab_settings = "Settings",
    .tab_accessibility = "Accessibility",
    .label_wifi_ssid = "Wi-Fi SSID",
    .label_wifi_password = "Wi-Fi Password",
    .label_mdns_target = "mDNS Target Host",
    .label_dark_theme = "Dark Theme",
    .label_use_fahrenheit = "Use °F",
    .label_apply = "Apply",
    .label_reset = "Reset",
    .label_wifi_connected = "Wi-Fi: Connected",
    .label_wifi_disconnected = "Wi-Fi: Disconnected",
    .label_crc_ok = "CRC: OK",
    .label_crc_error = "CRC: ERROR",
    .label_crc_unknown = "CRC: N/A",
    .label_temperature_prefix = "Temp",
    .label_humidity_prefix = "RH",
    .label_sensor_fallback = "Sensor",
    .label_sequence_prefix = "Seq",
    .label_language = "Language",
    .label_high_contrast = "High Contrast",
    .label_text_scale = "Text Scale",
    .label_touch_targets = "Large touch targets",
    .dropdown_languages = "English\nFrançais",
    .label_gpio_on = "ON",
    .label_gpio_off = "OFF",
    .label_frequency = "Frequency",
    .label_channel_prefix = "Channel",
};

static const ui_locale_pack_t s_pack_fr = {
    .tab_dashboard = "Tableau",
    .tab_gpio = "GPIO",
    .tab_pwm = "PWM",
    .tab_traces = "Courbes",
    .tab_settings = "Réglages",
    .tab_accessibility = "Accessibilité",
    .label_wifi_ssid = "SSID Wi-Fi",
    .label_wifi_password = "Mot de passe Wi-Fi",
    .label_mdns_target = "Hôte mDNS",
    .label_dark_theme = "Thème sombre",
    .label_use_fahrenheit = "Afficher °F",
    .label_apply = "Appliquer",
    .label_reset = "Réinitialiser",
    .label_wifi_connected = "Wi-Fi : Connecté",
    .label_wifi_disconnected = "Wi-Fi : Déconnecté",
    .label_crc_ok = "CRC : OK",
    .label_crc_error = "CRC : ERREUR",
    .label_crc_unknown = "CRC : N/A",
    .label_temperature_prefix = "Temp",
    .label_humidity_prefix = "HR",
    .label_sensor_fallback = "Capteur",
    .label_sequence_prefix = "Seq",
    .label_language = "Langue",
    .label_high_contrast = "Contraste élevé",
    .label_text_scale = "Taille du texte",
    .label_touch_targets = "Grandes zones tactiles",
    .dropdown_languages = "English\nFrançais",
    .label_gpio_on = "ACTIF",
    .label_gpio_off = "INACTIF",
    .label_frequency = "Fréquence",
    .label_channel_prefix = "Canal",
};

const ui_locale_pack_t *ui_locale_get_pack(hmi_language_t language)
{
    switch (language) {
        case HMI_LANGUAGE_FR:
            return &s_pack_fr;
        case HMI_LANGUAGE_EN:
        default:
            return &s_pack_en;
    }
}
