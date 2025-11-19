/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mcuboot_button.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "mcuboot_gpio_input.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#define BUTTON_0_NODE DT_ALIAS(button_pinhole)
#if DT_NODE_EXISTS(BUTTON_0_NODE) && DT_NODE_HAS_PROP(BUTTON_0_NODE, gpios)
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(BUTTON_0_NODE, gpios);
#else
#error "Unsupported board: button0 devicetree node label is not defined"
#endif

void
mcuboot_button_init(void)
{
    const struct gpio_dt_spec* const p_button = &button0;

    mcuboot_gpio_input_init(p_button, GPIO_PULL_UP, NULL, NULL, 0);
}

void
mcuboot_button_deinit(void)
{
    if (!device_is_ready(button0.port))
    {
        LOG_ERR("BUTTON0 is not ready");
        return;
    }

    const zephyr_api_ret_t rc = gpio_pin_configure_dt(&button0, GPIO_DISCONNECTED);
    if (0 != rc)
    {
        LOG_ERR("Failed to configure BUTTON0 (rc: %d)", rc);
        return;
    }
}

bool
mcuboot_button_get(void)
{
    zephyr_api_ret_t rc = gpio_pin_get_dt(&button0);
    if (rc < 0)
    {
        LOG_ERR("Failed to get BUTTON0 (rc: %d)", rc);
        return false;
    }
    return !!rc;
}
