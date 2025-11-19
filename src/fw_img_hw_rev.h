/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef HW_IMG_HW_REV_H
#define HW_IMG_HW_REV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/fs/fs.h>
#include "ruuvi_image_tlv.h"
#include "ruuvi_fa_id.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FW_INFO_HW_REV_NAME_MAX_LEN 15

typedef struct fw_image_hw_rev_t
{
    uint32_t hw_rev_num;
    char     hw_rev_name[FW_INFO_HW_REV_NAME_MAX_LEN + 1];
} fw_image_hw_rev_t;

bool
fw_img_hw_rev_find_in_flash_area(const fa_id_t fa_id, fw_image_hw_rev_t* const p_hw_rev);

bool
fw_img_hw_rev_find_in_file(struct fs_file_t* const p_file, fw_image_hw_rev_t* const p_hw_rev);

#ifdef __cplusplus
}
#endif

#endif // HW_IMG_HW_REV_H
