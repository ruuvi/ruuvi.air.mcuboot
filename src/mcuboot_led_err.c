/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mcuboot_led_err.h"
#include <stdint.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include "mcuboot_led.h"
#include "mcuboot_button.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#define DELAY_BEFORE_REBOOT_MS       100
#define LED_FLASH_DURATION_MS        100
#define BUTTON_PRESS_CHECK_PERIOD_MS 100
#define DELAY_BETWEEN_BLINKS_MS      900
#define DELAY_AFTER_FIRST_BLINK_MS   500

static bool
check_if_button_released_and_pressed(bool* p_is_button_released)
{
    if (!*p_is_button_released)
    {
        if (!mcuboot_button_get())
        {
            *p_is_button_released = true;
            LOG_INF("MCUboot: Button is released");
            LOG_INF("MCUboot: Wait until button is pressed to reboot");
        }
    }
    else
    {
        if (mcuboot_button_get())
        {
            LOG_INF("MCUboot: Button is pressed - reboot");
            k_busy_wait(DELAY_BEFORE_REBOOT_MS * USEC_PER_MSEC);
            return true;
        }
    }
    return false;
}

__NO_RETURN void
mcuboot_led_err_blink_red_led(const uint32_t num_red_blinks)
{
    bool is_button_released = !mcuboot_button_get();
    if (is_button_released)
    {
        LOG_INF("MCUboot: Wait until button is pressed to reboot");
    }
    mcuboot_led_green_off();
    while (1)
    {
        mcuboot_led_red_on();
        k_busy_wait(LED_FLASH_DURATION_MS * USEC_PER_MSEC);
        if (check_if_button_released_and_pressed(&is_button_released))
        {
            sys_reboot(SYS_REBOOT_COLD);
        }
        mcuboot_led_red_off();
        for (uint32_t i = 0; i < (DELAY_AFTER_FIRST_BLINK_MS / BUTTON_PRESS_CHECK_PERIOD_MS); ++i)
        {
            k_busy_wait(BUTTON_PRESS_CHECK_PERIOD_MS * USEC_PER_MSEC);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }

        for (uint32_t i = 0; i < num_red_blinks; ++i)
        {
            mcuboot_led_red_on();
            k_busy_wait(LED_FLASH_DURATION_MS * USEC_PER_MSEC);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
            mcuboot_led_red_off();
            k_busy_wait(BUTTON_PRESS_CHECK_PERIOD_MS * USEC_PER_MSEC);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
        for (int32_t i = 0; i < (DELAY_BETWEEN_BLINKS_MS / BUTTON_PRESS_CHECK_PERIOD_MS); ++i)
        {
            k_busy_wait(BUTTON_PRESS_CHECK_PERIOD_MS * USEC_PER_MSEC);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
    }
}
