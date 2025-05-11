#define DT_DRV_COMPAT zmk_behavior_sensor_printer

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>

#define MAX_FORMATTED_CHAR_SIZE 24
#define SENSOR_PRINTER_TYPE_DELAY_MS 10

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if 1 || DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_sensor_printer_data
{
    const uint32_t dot_keycode;
    struct k_work_delayable typing_work;
    uint8_t chars[MAX_FORMATTED_CHAR_SIZE];
    uint8_t chars_len;
    uint8_t current_idx;
    bool key_pressed;
};

struct behavior_sensor_printer_config
{
    const struct device *sensor;
    enum sensor_channel channel;
    uint32_t decimal_places;
    uint32_t decimal_separator;
};

#define CHAR_MAP_DOT 10
#define CHAR_MAP_MINUS 11

static inline void reset_typing_state(struct behavior_sensor_printer_data *data)
{
    data->current_idx = 0;
    data->key_pressed = false;
    memset(data->chars, 0, sizeof(data->chars));
}

static void send_key(struct behavior_sensor_printer_data *data)
{
    if (data->current_idx >= data->chars_len || data->current_idx >= ARRAY_SIZE(data->chars))
    {
        // All keys typed, reset state
        reset_typing_state(data);
        return;
    }

    uint8_t current_char = data->chars[data->current_idx];
    uint32_t keycode = 0;

    switch (current_char)
    {
    case '0':
        keycode = NUMBER_0;
        break;
    case '1':
        keycode = NUMBER_1;
        break;
    case '2':
        keycode = NUMBER_2;
        break;
    case '3':
        keycode = NUMBER_3;
        break;
    case '4':
        keycode = NUMBER_4;
        break;
    case '5':
        keycode = NUMBER_5;
        break;
    case '6':
        keycode = NUMBER_6;
        break;
    case '7':
        keycode = NUMBER_7;
        break;
    case '8':
        keycode = NUMBER_8;
        break;
    case '9':
        keycode = NUMBER_9;
        break;
    case '-':
        keycode = MINUS;
        break;
    case '.':
        keycode = data->dot_keycode;
        break;
    default:
        LOG_ERR("behavior_sensor_printer: Invalid character %d at index %d", current_char, data->current_idx);
        reset_typing_state(data);
        return;
    }

    // if the key is currently pressed, we need to release it
    bool pressed = !data->key_pressed;
    raise_zmk_keycode_state_changed_from_encoded(keycode, pressed, k_uptime_get());
    data->key_pressed = pressed;

    if (pressed)
    {
        // If the key is pressed, schedule release
        k_work_schedule(&data->typing_work, K_MSEC(SENSOR_PRINTER_TYPE_DELAY_MS));
        return;
    }
    else
    {
        // If the key is released, move to the next character
        data->current_idx++;

        if (data->current_idx < ARRAY_SIZE(data->chars) && data->current_idx < data->chars_len)
        {
            k_work_schedule(&data->typing_work, K_MSEC(SENSOR_PRINTER_TYPE_DELAY_MS));
        }
        else
        {
            reset_typing_state(data);
        }
    }
}

static void type_keys_work(struct k_work *work)
{
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct behavior_sensor_printer_data *data = CONTAINER_OF(dwork, struct behavior_sensor_printer_data, typing_work);
    send_key(data);
}

static int behavior_sensor_printer_init(const struct device *dev)
{
    struct behavior_sensor_printer_data *data = dev->data;
    k_work_init_delayable(&data->typing_work, type_keys_work);

    const struct behavior_sensor_printer_config *config = dev->config;
    const struct device *sensor = config->sensor;
    if (!device_is_ready(sensor))
    {
        LOG_ERR("Sensor device \"%s\" is not ready", sensor->name);
        return -ENODEV;
    }

    int rc = sensor_sample_fetch(sensor);
    if (rc < 0)
    {
        LOG_ERR("Failed to fetch sensor sample: %d", rc);
        return rc;
    }

    return 0;
};

#define MILLION INT64_C(1000000)

static void format_sensor_value(const struct sensor_value *val, const uint8_t decimal_places, uint8_t *buffer, uint8_t *length)
{
    // *Slightly* over engineered and way too complicated for what it is, but
    // it works and I'm too lazy to rewrite it.
    // Could be way simpler in hindsight.

    int64_t total_scaled = (int64_t)val->val1 * MILLION + val->val2;
    bool is_negative = total_scaled < 0;
    uint64_t abs_total = is_negative ? -total_scaled : total_scaled;

    uint32_t integer_part = (uint32_t)(abs_total / MILLION);
    uint32_t fractional_part = (uint32_t)(abs_total % MILLION);
    bool is_zero = (integer_part == 0) && (fractional_part == 0);

    uint8_t buffer_idx = 0;

    if (!is_zero && is_negative)
    {
        buffer[buffer_idx++] = '-';
    }

    if (integer_part == 0)
    {
        buffer[buffer_idx++] = '0';
    }
    else
    {
        char temp[10];
        uint8_t temp_idx = 0;
        while (integer_part > 0)
        {
            temp[temp_idx++] = '0' + (integer_part % 10);
            integer_part /= 10;
        }
        while (temp_idx > 0)
        {
            buffer[buffer_idx++] = temp[--temp_idx];
        }
    }

    if (decimal_places > 0)
    {
        uint64_t pow10 = 1;
        for (int i = 0; i < decimal_places; i++)
        {
            pow10 *= 10;
        }

        uint64_t fractional_digits = (uint64_t)fractional_part * pow10 / MILLION;

        if (fractional_digits != 0)
        {
            buffer[buffer_idx++] = '.';
            uint8_t start_frac = buffer_idx;

            for (int i = decimal_places - 1; i >= 0; i--)
            {
                buffer[buffer_idx + i] = '0' + (fractional_digits % 10);
                fractional_digits /= 10;
            }
            buffer_idx += decimal_places;

            uint8_t frac_end = buffer_idx - 1;
            while (frac_end >= start_frac && buffer[frac_end] == '0')
            {
                frac_end--;
            }

            if (frac_end < start_frac)
            {
                buffer_idx = start_frac - 1;
            }
            else
            {
                buffer_idx = frac_end + 1;
            }
        }
    }

    *length = buffer_idx;
}

static int on_sensor_printer_binding_pressed(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event)
{
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_sensor_printer_config *config = dev->config;
    struct behavior_sensor_printer_data *data = dev->data;

    if (data->key_pressed || data->current_idx)
    {
        // Typing in progress, ignore
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // Get the sensor value
    struct sensor_value value;
    int ret = sensor_sample_fetch_chan(config->sensor, config->channel);
    if (ret < 0)
    {
        LOG_ERR("behavior_sensor_printer: Failed to fetch sensor sample: %d", ret);
        return ret;
    }
    ret = sensor_channel_get(config->sensor, config->channel, &value);
    if (ret < 0)
    {
        LOG_ERR("behavior_sensor_printer: Failed to get sensor value: %d", ret);
        return ret;
    }

    LOG_DBG("behavior_sensor_printer: Sensor value: %d.%06d from sensor %s", value.val1, value.val2, config->sensor->name);

    // Convert the sensor value to a string
    reset_typing_state(data);
    format_sensor_value(&value, config->decimal_places, data->chars, &data->chars_len);

    LOG_DBG("behavior_sensor_printer: Formatted value (len %u): %s", data->chars_len, data->chars);

    // Press the first key
    send_key(data);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_sensor_printer_binding_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}

// API Structure
static const struct behavior_driver_api behavior_sensor_printer_driver_api = {
    .binding_pressed = on_sensor_printer_binding_pressed,
    .binding_released = on_sensor_printer_binding_released,
};

#define KP_INST(idx)                                                                            \
    static struct behavior_sensor_printer_data behavior_sensor_printer_data_##idx = {           \
        .dot_keycode = DT_INST_PROP(idx, decimal_separator),                                    \
    };                                                                                          \
    const static struct behavior_sensor_printer_config behavior_sensor_printer_config_##idx = { \
        .sensor = DEVICE_DT_GET(DT_INST_PHANDLE(idx, sensor)),                                  \
        .channel = DT_INST_PROP(idx, channel),                                                  \
        .decimal_places = DT_INST_PROP(idx, decimal_places),                                    \
        .decimal_separator = DT_INST_PROP(idx, decimal_separator),                              \
    };                                                                                          \
    BUILD_ASSERT(0 <= DT_INST_PROP(idx, decimal_places) &&                                      \
                     DT_INST_PROP(idx, decimal_places) <= 6,                                    \
                 "decimal_places must between 0 and 6");                                        \
    BEHAVIOR_DT_INST_DEFINE(idx, behavior_sensor_printer_init, NULL,                            \
                            &behavior_sensor_printer_data_##idx,                                \
                            &behavior_sensor_printer_config_##idx,                              \
                            POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                           \
                            &behavior_sensor_printer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
