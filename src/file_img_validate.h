/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef FILE_IMG_VALIDATE_H
#define FILE_IMG_VALIDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <zephyr/fs/fs_interface.h>
#include <bootutil/image.h>
#include <bootutil/fault_injection_hardening.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Verify the integrity of the image.
 * Return non-zero if image could not be validated/does not validate.
 */
fih_ret
file_img_validate(
    const struct image_header* const hdr,
    struct fs_file_t* const          p_file,
    const uint32_t                   fa_size,
    uint8_t* const                   tmp_buf,
    const uint32_t                   tmp_buf_sz,
    const uint8_t* const             seed,
    const ssize_t                    seed_len);

#ifdef __cplusplus
}
#endif

#endif // FILE_IMG_VALIDATE_H
