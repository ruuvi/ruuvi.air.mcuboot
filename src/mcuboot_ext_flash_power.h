/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_FLASH_POWER_H)
#define MCUBOOT_FLASH_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_ext_flash_power_on(void);

void
mcuboot_ext_flash_power_off(void);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_FLASH_POWER_H
