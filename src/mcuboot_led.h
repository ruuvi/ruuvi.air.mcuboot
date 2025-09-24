/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_LED_H)
#define MCUBOOT_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_led_init(void);

void
mcuboot_led_deinit(void);

void
mcuboot_led_red_set(const bool is_on);

static inline void
mcuboot_led_red_on(void)
{
    mcuboot_led_red_set(true);
}

static inline void
mcuboot_led_red_off(void)
{
    mcuboot_led_red_set(false);
}

void
mcuboot_led_green_set(const bool is_on);

static inline void
mcuboot_led_green_on(void)
{
    mcuboot_led_green_set(true);
}

static inline void
mcuboot_led_green_off(void)
{
    mcuboot_led_green_set(false);
}

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_LED_H
