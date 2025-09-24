/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_GPIO_H)
#define MCUBOOT_GPIO_H

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_gpio_input_init(
    const struct gpio_dt_spec* const p_gpio_dt,
    gpio_flags_t                     extra_flags,
    struct gpio_callback* const      p_gpio_callback,
    const gpio_callback_handler_t    cb_handler,
    const gpio_flags_t               int_flags);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_GPIO_H
