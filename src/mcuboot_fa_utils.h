/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_FA_UTILS_H)
#define MCUBOOT_FA_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include "ruuvi_fa_id.h"
#include "bootutil/image.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct image_version mcuboot_s0_s1_image_version;

bool
get_flash_area_address_and_size(const fa_id_t fa_id, uint32_t* const p_fa_addr, uint32_t* const p_fa_size);

const char*
get_image_slot_name(const fa_id_t fa_id);

bool
load_image_header(const fa_id_t fa_id, struct image_header* const p_img_hdr);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_FA_UTILS_H
