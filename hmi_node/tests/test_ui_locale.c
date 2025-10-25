#include "display/ui_locale.h"

#include "unity.h"

TEST_CASE("ui locale defaults to english when out of range", "[hmi][ui][locale]")
{
    const ui_locale_pack_t *pack = ui_locale_get_pack(HMI_LANGUAGE_MAX);
    TEST_ASSERT_NOT_NULL(pack);
    TEST_ASSERT_EQUAL_STRING("Dashboard", pack->tab_dashboard);
    TEST_ASSERT_EQUAL_STRING("ON", pack->label_gpio_on);
}

TEST_CASE("ui locale provides french strings", "[hmi][ui][locale]")
{
    const ui_locale_pack_t *pack = ui_locale_get_pack(HMI_LANGUAGE_FR);
    TEST_ASSERT_NOT_NULL(pack);
    TEST_ASSERT_EQUAL_STRING("Réglages", pack->tab_settings);
    TEST_ASSERT_EQUAL_STRING("INACTIF", pack->label_gpio_off);
}
