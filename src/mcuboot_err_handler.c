/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <kernel_arch_interface.h>
#include "mcuboot_led_err.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

void
k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf)
{
    ARG_UNUSED(esf);

    LOG_ERR("MCUboot: System fatal error, reason %d", reason);
    arch_system_halt(reason);
    CODE_UNREACHABLE;
}

FUNC_NORETURN void
arch_system_halt(unsigned int reason)
{
    LOG_ERR("MCUboot: arch_system_halt: reason %d", reason);
    (void)arch_irq_lock();
    mcuboot_led_err_blink_red_led(NUM_RED_LED_BLINKS_ON_HALT_SYSTEM);
    CODE_UNREACHABLE;
}

void
assert_post_action(const char* file, unsigned int line)
{
    LOG_ERR("### MCUboot: Assertion failed at %s:%u", file, line);
    mcuboot_led_err_blink_red_led(NUM_RED_LED_BLINKS_ON_ASSERT);
}
