/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MCUBOOT_IMG_OP_H
#define MCUBOOT_IMG_OP_H

#include <stdbool.h>
#include <zephyr/fs/fs_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
mcuboot_img_op_copy(const int fa_id_dst, struct fs_file_t* const p_file_src);

bool
mcuboot_img_op_cmp(const int fa_id_dst, struct fs_file_t* const p_file_src);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_IMG_OP_H
