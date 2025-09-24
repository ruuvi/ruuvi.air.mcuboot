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
            k_busy_wait(100 * 1000);
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
        k_busy_wait(100 * 1000);
        if (check_if_button_released_and_pressed(&is_button_released))
        {
            sys_reboot(SYS_REBOOT_COLD);
        }
        mcuboot_led_red_off();
        for (uint32_t i = 0; i < 5; ++i)
        {
            k_busy_wait(100 * 1000);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }

        for (uint32_t i = 0; i < num_red_blinks; ++i)
        {
            mcuboot_led_red_on();
            k_busy_wait(100 * 1000);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
            mcuboot_led_red_off();
            k_busy_wait(100 * 1000);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
        for (int i = 0; i < 9; ++i)
        {
            k_busy_wait(100 * 1000);
            if (check_if_button_released_and_pressed(&is_button_released))
            {
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
    }
}
