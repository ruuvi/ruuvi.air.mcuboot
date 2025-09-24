/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_LED_ERR_H)
#define MCUBOOT_LED_ERR_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_RED_LED_BLINKS_ON_HALT_SYSTEM (3)
#define NUM_RED_LED_BLINKS_ON_ASSERT      (4)

__NO_RETURN void
mcuboot_led_err_blink_red_led(const uint32_t num_red_blinks);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_LED_ERR_H
