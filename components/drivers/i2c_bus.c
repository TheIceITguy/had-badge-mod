/* See drivers/i2c_bus.h. */
#include "drivers/i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

/* One slot per hardware port. Two on the ESP32-S3, but the array is sized from
 * the SOC header so this does not need revisiting on another target. */
#define MAX_PORTS SOC_I2C_NUM

static struct {
    i2c_master_bus_handle_t handle;
    int sda, scl;
    bool up;
} s_bus[MAX_PORTS];

esp_err_t i2c_bus_get(int port, int sda_pin, int scl_pin, i2c_master_bus_handle_t *out)
{
    if (port < 0 || port >= MAX_PORTS) return ESP_ERR_INVALID_ARG;

    if (s_bus[port].up) {
        if (s_bus[port].sda != sda_pin || s_bus[port].scl != scl_pin)
            ESP_LOGW(TAG, "port %d is already up on sda=%d scl=%d; ignoring the request for "
                          "sda=%d scl=%d (two settings disagree about this bus)",
                     port, s_bus[port].sda, s_bus[port].scl, sda_pin, scl_pin);
        *out = s_bus[port].handle;
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus[port].handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "port %d on sda=%d scl=%d: %s", port, sda_pin, scl_pin,
                 esp_err_to_name(e));
        return e;
    }

    s_bus[port].sda = sda_pin;
    s_bus[port].scl = scl_pin;
    s_bus[port].up = true;
    *out = s_bus[port].handle;
    return ESP_OK;
}
