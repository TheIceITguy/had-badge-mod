/* See drivers/vibe.h. A GPIO high vibrates the motor and an esp_timer one-shot
 * puts it back down, so callers fire and forget. */
#include "drivers/vibe.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "vibe";

static int s_pin = -1;          /* -1 = no motor configured */
static esp_timer_handle_t s_off;

/* Runs in the esp_timer task, so it touches nothing but the pin. */
static void vibe_stop(void *arg)
{
    (void)arg;
    if (s_pin >= 0) gpio_set_level((gpio_num_t)s_pin, 0);
}

esp_err_t vibe_init(int pin)
{
    if (pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        ESP_LOGE(TAG, "GPIO%d cannot drive an output", pin);
        return ESP_ERR_INVALID_ARG;
    }

    /* Pull down as well as drive low: the motor must stay still through the
     * window between reset and this call, not buzz until the firmware is up. */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "gpio %d: %s", pin, esp_err_to_name(e)); return e; }
    gpio_set_level((gpio_num_t)pin, 0);

    const esp_timer_create_args_t targs = { .callback = vibe_stop, .name = "vibe" };
    e = esp_timer_create(&targs, &s_off);
    if (e != ESP_OK) { ESP_LOGE(TAG, "timer: %s", esp_err_to_name(e)); return e; }

    s_pin = pin;
    ESP_LOGI(TAG, "vibration motor on GPIO%d", pin);
    return ESP_OK;
}

void vibe_pulse(int ms)
{
    if (s_pin < 0 || ms <= 0) return;
    if (ms > VIBE_MAX_MS) ms = VIBE_MAX_MS;

    /* Restart, never stack: esp_timer_start_once fails on a running timer, and
     * a second message arriving mid-buzz should extend the buzz, not be lost. */
    esp_timer_stop(s_off);
    gpio_set_level((gpio_num_t)s_pin, 1);
    esp_timer_start_once(s_off, (uint64_t)ms * 1000);
}

bool vibe_available(void) { return s_pin >= 0; }
