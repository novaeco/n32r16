#include "display/ui_screens.h"

#include "unity.h"
#include <string.h>

static struct {
    uint8_t device_index;
    uint8_t port;
    uint16_t mask;
    uint16_t value;
    int calls;
} s_gpio_invocation;

static struct {
    uint8_t channel;
    uint16_t duty;
    int calls;
} s_pwm_invocation;

static struct {
    uint16_t freq;
    int calls;
} s_pwm_freq_invocation;

static struct {
    hmi_user_preferences_t prefs;
    int calls;
} s_prefs_invocation;

static void reset_state(void)
{
    memset(&s_gpio_invocation, 0, sizeof(s_gpio_invocation));
    memset(&s_pwm_invocation, 0, sizeof(s_pwm_invocation));
    memset(&s_pwm_freq_invocation, 0, sizeof(s_pwm_freq_invocation));
    memset(&s_prefs_invocation, 0, sizeof(s_prefs_invocation));
}

static void fake_write_gpio(uint8_t device_index, uint8_t port, uint16_t mask, uint16_t value, void *ctx)
{
    (void)ctx;
    s_gpio_invocation.device_index = device_index;
    s_gpio_invocation.port = port;
    s_gpio_invocation.mask = mask;
    s_gpio_invocation.value = value;
    ++s_gpio_invocation.calls;
}

static void fake_set_pwm(uint8_t channel, uint16_t duty, void *ctx)
{
    (void)ctx;
    s_pwm_invocation.channel = channel;
    s_pwm_invocation.duty = duty;
    ++s_pwm_invocation.calls;
}

static void fake_set_pwm_frequency(uint16_t freq, void *ctx)
{
    (void)ctx;
    s_pwm_freq_invocation.freq = freq;
    ++s_pwm_freq_invocation.calls;
}

static void fake_apply_prefs(const hmi_user_preferences_t *prefs, void *ctx)
{
    (void)ctx;
    s_prefs_invocation.prefs = *prefs;
    ++s_prefs_invocation.calls;
}

void setUp(void)
{
    reset_state();
    ui_callbacks_t callbacks = {
        .set_pwm = fake_set_pwm,
        .set_pwm_frequency = fake_set_pwm_frequency,
        .write_gpio = fake_write_gpio,
        .apply_preferences = fake_apply_prefs,
    };
    ui_set_callbacks_for_test(&callbacks, NULL);
    hmi_user_preferences_t defaults = {
        .ssid = "net",
        .password = "pass",
        .mdns_target = "sensor",
        .dark_theme = false,
        .use_fahrenheit = false,
    };
    ui_set_active_prefs_for_test(&defaults);
}

void tearDown(void)
{
    ui_set_callbacks_for_test(NULL, NULL);
}

TEST_CASE("ui gpio switch dispatch computes mask and value", "[hmi][ui]")
{
    ui_test_handle_gpio_switch(1, 9, true);
    TEST_ASSERT_EQUAL(1, s_gpio_invocation.calls);
    TEST_ASSERT_EQUAL_UINT8(1, s_gpio_invocation.device_index);
    TEST_ASSERT_EQUAL_UINT8(1, s_gpio_invocation.port);
    TEST_ASSERT_EQUAL_UINT16(0x0002, s_gpio_invocation.mask);
    TEST_ASSERT_EQUAL_UINT16(0x0002, s_gpio_invocation.value);

    ui_test_handle_gpio_switch(1, 9, false);
    TEST_ASSERT_EQUAL(2, s_gpio_invocation.calls);
    TEST_ASSERT_EQUAL_UINT16(0x0000, s_gpio_invocation.value);
}

TEST_CASE("ui pwm handlers forward slider and frequency changes", "[hmi][ui]")
{
    ui_test_handle_pwm_slider(5, 1234);
    TEST_ASSERT_EQUAL(1, s_pwm_invocation.calls);
    TEST_ASSERT_EQUAL_UINT8(5, s_pwm_invocation.channel);
    TEST_ASSERT_EQUAL_UINT16(1234, s_pwm_invocation.duty);

    ui_test_handle_pwm_frequency(789);
    TEST_ASSERT_EQUAL(1, s_pwm_freq_invocation.calls);
    TEST_ASSERT_EQUAL_UINT16(789, s_pwm_freq_invocation.freq);
}

TEST_CASE("ui preferences dispatcher merges and forwards", "[hmi][ui]")
{
    ui_test_apply_preferences_inputs("new-ssid", "secret", "target", true, true);
    TEST_ASSERT_EQUAL(1, s_prefs_invocation.calls);
    TEST_ASSERT_EQUAL_STRING("new-ssid", s_prefs_invocation.prefs.ssid);
    TEST_ASSERT_EQUAL_STRING("secret", s_prefs_invocation.prefs.password);
    TEST_ASSERT_EQUAL_STRING("target", s_prefs_invocation.prefs.mdns_target);
    TEST_ASSERT_TRUE(s_prefs_invocation.prefs.dark_theme);
    TEST_ASSERT_TRUE(s_prefs_invocation.prefs.use_fahrenheit);
}
