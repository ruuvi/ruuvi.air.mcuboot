/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef FILE_TLV_H
#define FILE_TLV_H

#include <stdint.h>
#include <stdbool.h>
#include <bootutil/image.h>
#include <zephyr/fs/fs.h>
#include "zephyr_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct file_tlv_iter_t
{
    const struct image_header* hdr;
    struct fs_file_t*          p_file;
    uint16_t                   type;
    bool                       prot;
    uint32_t                   prot_end;
    uint32_t                   tlv_off;
    uint32_t                   tlv_end;
} file_tlv_iter_t;

/*
 * Initialize a TLV iterator.
 *
 * @param it An iterator struct
 * @param hdr image_header of the slot's image
 * @param p_file fs_file_t of the opened file which is storing the image
 * @param type Type of TLV to look for
 * @param prot true if TLV has to be stored in the protected area, false otherwise
 *
 * @returns 0 if the TLV iterator was successfully started
 *          -1 on errors
 */
zephyr_api_ret_t
file_tlv_iter_begin(
    file_tlv_iter_t* const           it,
    const struct image_header* const hdr,
    struct fs_file_t* const          p_file,
    const uint16_t                   type,
    const bool                       prot);

/*
 * Find next TLV
 *
 * @param it The image TLV iterator struct
 * @param off The offset of the TLV's payload in flash
 * @param len The length of the TLV's payload
 * @param type If not NULL returns the type of TLV found
 *
 * @returns 0 if a TLV with with matching type was found
 *          1 if no more TLVs with matching type are available
 *          -1 on errors
 */
zephyr_api_ret_t
file_tlv_iter_next(file_tlv_iter_t* const it, uint32_t* const off, uint16_t* const len, uint16_t* const type);

/*
 * Return if a TLV entry is in the protected area.
 *
 * @param it The image TLV iterator struct
 * @param off The offset of the entry to check.
 *
 * @return 0 if this TLV iterator entry is not protected.
 *         1 if this TLV iterator entry is in the protected region
 *         -1 if the iterator is invalid.
 */
zephyr_api_ret_t
file_tlv_iter_is_prot(const file_tlv_iter_t* const it, const uint32_t off);

#ifdef __cplusplus
}
#endif

#endif // FILE_TLV_H
