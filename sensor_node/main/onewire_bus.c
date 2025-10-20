#include "onewire_bus.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static gpio_num_t s_onewire_gpio;

esp_err_t onewire_bus_init(gpio_num_t gpio) {
    s_onewire_gpio = gpio;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static inline void line_drive_low(uint32_t duration_us) {
    gpio_set_direction(s_onewire_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_onewire_gpio, 0);
    ets_delay_us(duration_us);
    gpio_set_level(s_onewire_gpio, 1);
}

static inline int line_sample(void) {
    gpio_set_direction(s_onewire_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    return gpio_get_level(s_onewire_gpio);
}

bool onewire_bus_reset(void) {
    taskENTER_CRITICAL();
    line_drive_low(480);
    ets_delay_us(70);
    int presence = line_sample();
    ets_delay_us(410);
    taskEXIT_CRITICAL();
    return presence == 0;
}

static void onewire_write_bit(int bit) {
    taskENTER_CRITICAL();
    line_drive_low(bit ? 6 : 60);
    if (bit) {
        ets_delay_us(64);
    } else {
        ets_delay_us(10);
    }
    taskEXIT_CRITICAL();
}

static int onewire_read_bit(void) {
    int level;
    taskENTER_CRITICAL();
    line_drive_low(6);
    ets_delay_us(9);
    level = line_sample();
    ets_delay_us(55);
    taskEXIT_CRITICAL();
    return level;
}

bool onewire_bus_read(uint8_t *buffer, size_t len) {
    if (buffer == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            value |= (onewire_read_bit() << bit);
        }
        buffer[i] = value;
    }
    return true;
}

bool onewire_bus_write(const uint8_t *buffer, size_t len) {
    if (buffer == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        uint8_t value = buffer[i];
        for (int bit = 0; bit < 8; ++bit) {
            onewire_write_bit(value & 0x01);
            value >>= 1;
        }
    }
    return true;
}

bool onewire_bus_search(onewire_device_list_t *list) {
    if (list == NULL) {
        return false;
    }

    uint8_t rom_no[8] = {0};
    int last_discrepancy = 0;
    int last_device_flag = 0;

    list->count = 0;

    while (!last_device_flag && list->count < ONEWIRE_MAX_DEVICES) {
        if (!onewire_bus_reset()) {
            break;
        }
        uint8_t search_cmd = 0xF0;
        onewire_bus_write(&search_cmd, 1);

        int id_bit_number = 1;
        int rom_byte_number = 0;
        uint8_t rom_byte_mask = 1;
        int discrepancy_marker = 0;

        while (rom_byte_number < 8) {
            int id_bit = onewire_read_bit();
            int cmp_id_bit = onewire_read_bit();

            if (id_bit == 1 && cmp_id_bit == 1) {
                break;
            }

            int search_direction;
            if (id_bit != cmp_id_bit) {
                search_direction = id_bit;
            } else {
                if (id_bit_number < last_discrepancy) {
                    search_direction = ((rom_no[rom_byte_number] & rom_byte_mask) > 0);
                } else {
                    search_direction = (id_bit_number == last_discrepancy);
                }
                if (search_direction == 0) {
                    discrepancy_marker = id_bit_number;
                }
            }

            if (search_direction == 1) {
                rom_no[rom_byte_number] |= rom_byte_mask;
            } else {
                rom_no[rom_byte_number] &= (uint8_t)~rom_byte_mask;
            }

            onewire_write_bit(search_direction);

            id_bit_number++;
            rom_byte_mask <<= 1;
            if (rom_byte_mask == 0) {
                rom_byte_number++;
                rom_byte_mask = 1;
            }
        }

        if (id_bit_number > 64) {
            last_discrepancy = discrepancy_marker;
            if (last_discrepancy == 0) {
                last_device_flag = 1;
            }
            uint64_t rom = 0;
            for (int i = 0; i < 8; ++i) {
                rom |= ((uint64_t)rom_no[i]) << (8 * i);
            }
            list->roms[list->count++] = rom;
        } else {
            break;
        }
    }

    return true;
}

