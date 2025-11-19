/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MCUBOOT_IMG_OP_H
#define MCUBOOT_IMG_OP_H

#include <stdbool.h>
#include <zephyr/fs/fs_interface.h>
#include "ruuvi_fa_id.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
mcuboot_img_op_copy(const fa_id_t fa_id_dst, struct fs_file_t* const p_file_src);

bool
mcuboot_img_op_cmp(const fa_id_t fa_id_dst, struct fs_file_t* const p_file_src);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_IMG_OP_H
