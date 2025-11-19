/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "mcuboot_supercap.h"
#include "mcuboot_led.h"
#include "mcuboot_button.h"
#include "mcuboot_ext_flash_power.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#define BUTTON_0_DETECT_DELAY_MS 1000

#define CONFIG_RUUVI_AIR_GPIO_EXT_FLASH_POWER_ON_PRIORITY 41
_Static_assert(CONFIG_RUUVI_AIR_GPIO_EXT_FLASH_POWER_ON_PRIORITY > CONFIG_GPIO_INIT_PRIORITY);
_Static_assert(CONFIG_RUUVI_AIR_GPIO_EXT_FLASH_POWER_ON_PRIORITY < CONFIG_NORDIC_QSPI_NOR_INIT_PRIORITY);

static int // NOSONAR: Zephyr init functions must return int
mcuboot_early_init_post_kernel(void)
{
    printk("\r\n*** %s ***\r\n", CONFIG_NCS_APPLICATION_BOOT_BANNER_STRING);
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    mcuboot_supercap_init();
#endif // CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1
    mcuboot_led_init();
    mcuboot_button_init();
    mcuboot_ext_flash_power_on();
    return 0;
}

SYS_INIT(mcuboot_early_init_post_kernel, POST_KERNEL, CONFIG_RUUVI_AIR_GPIO_EXT_FLASH_POWER_ON_PRIORITY);

static int // NOSONAR: Zephyr init functions must return int
mcuboot_early_init_application(void)
{
    return 0;
}

SYS_INIT(mcuboot_early_init_application, APPLICATION, 0);
