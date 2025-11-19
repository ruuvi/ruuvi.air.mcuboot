/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_FW_UPDATE_H)
#define MCUBOOT_FW_UPDATE_H

#include "fw_img_hw_rev.h"
#include "ruuvi_fa_id.h"

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_fw_update(const slot_id_t mcuboot_active_slot, const fw_image_hw_rev_t* const p_hw_rev);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_FW_UPDATE_H
