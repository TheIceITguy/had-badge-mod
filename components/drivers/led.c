/* See drivers/led.h. One GPIO, one level, no timers: the service decides when. */
#include "drivers/led.h"

#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "led";

static int s_pin = -1;      /* -1 = no LED configured */
static bool s_active_low;

esp_err_t led_init(int pin, bool active_low)
{
    if (pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        ESP_LOGE(TAG, "GPIO%d cannot drive an output", pin);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "gpio %d: %s", pin, esp_err_to_name(e)); return e; }

    s_pin = pin;
    s_active_low = active_low;
    led_set(false);
    ESP_LOGI(TAG, "notification LED on GPIO%d (%s)", pin, active_low ? "active low" : "active high");
    return ESP_OK;
}

void led_set(bool on)
{
    if (s_pin < 0) return;
    gpio_set_level((gpio_num_t)s_pin, (uint32_t)(s_active_low ? !on : on));
}

bool led_available(void) { return s_pin >= 0; }
