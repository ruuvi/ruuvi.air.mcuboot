/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mcuboot_led.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <io/io.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#if DT_NODE_EXISTS(DT_ALIAS(led_red))
#define LED_RED_NODE DT_ALIAS(led_red)
#else
#error "'led-red' devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED_RED_NODE, okay) && DT_NODE_HAS_PROP(LED_RED_NODE, gpios)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
#else
#error "'led-red' devicetree alias is not defined properly"
#endif

static void
mcuboot_led_init_gpio(const struct gpio_dt_spec* p_led_spec)
{
    if (!device_is_ready(p_led_spec->port))
    {
        LOG_ERR("LED %s:%d is not ready", p_led_spec->port->name, p_led_spec->pin);
        return;
    }

    const int32_t rc = gpio_pin_configure_dt(p_led_spec, GPIO_OUTPUT_INACTIVE);
    if (0 != rc)
    {
        LOG_ERR("Failed to configure LED %s:%d, rc %d", p_led_spec->port->name, p_led_spec->pin, rc);
        return;
    }
}

void
mcuboot_led_init(void)
{
    mcuboot_led_init_gpio(&led_red);
}

static void
mcuboot_led_deinit_gpio(const struct gpio_dt_spec* p_led_spec)
{
    if (!device_is_ready(p_led_spec->port))
    {
        LOG_ERR("LED %s:%d is not ready", p_led_spec->port->name, p_led_spec->pin);
        return;
    }

    gpio_pin_set_dt(p_led_spec, 0);

    const int32_t rc = gpio_pin_configure_dt(p_led_spec, GPIO_DISCONNECTED);
    if (0 != rc)
    {
        LOG_ERR("Failed to configure LED %s:%d, rc %d", p_led_spec->port->name, p_led_spec->pin, rc);
        return;
    }
}

void
mcuboot_led_deinit(void)
{
    mcuboot_led_deinit_gpio(&led_red);
}

void
mcuboot_led_red_set(const bool is_on)
{
    gpio_pin_set_dt(&led_red, is_on ? 1 : 0);
}

void
mcuboot_led_green_set(const bool is_on)
{
    io_led_set(is_on);
}
