/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MCUBOOT_BUTTON_H
#define MCUBOOT_BUTTON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_button_init(void);

void
mcuboot_button_deinit(void);

bool
mcuboot_button_get(void);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_BUTTON_H
