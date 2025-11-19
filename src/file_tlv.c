/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2019 JUUL Labs
 * Copyright (c) 2020 Arm Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This is a port of the original file: mcuboot/boot/bootutil/src/tlv.c
 */

#include "file_tlv.h"
#include <stddef.h>

#include <zephyr/fs/fs.h>
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "file_tlv_priv.h"
#include "zephyr_api.h"

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
    const bool                       prot)
{
    if ((NULL == it) || (NULL == hdr) || (NULL == p_file))
    {
        return -1;
    }

    uint32_t              offset = BOOT_TLV_OFF(hdr);
    struct image_tlv_info info   = { 0 };
    if (LOAD_IMAGE_DATA(hdr, p_file, offset, (void*)&info, sizeof(info)))
    {
        return -1;
    }

    if (IMAGE_TLV_PROT_INFO_MAGIC == info.it_magic)
    {
        if (hdr->ih_protect_tlv_size != info.it_tlv_tot)
        {
            return -1;
        }

        if (LOAD_IMAGE_DATA(hdr, p_file, offset + info.it_tlv_tot, (void*)&info, sizeof(info)))
        {
            return -1;
        }
    }
    else if (0 != hdr->ih_protect_tlv_size)
    {
        return -1;
    }
    else
    {
        // MISRA: "if ... else if" constructs should end with "else" clauses
    }

    if (IMAGE_TLV_INFO_MAGIC != info.it_magic)
    {
        return -1;
    }

    it->hdr      = hdr;
    it->p_file   = p_file;
    it->type     = type;
    it->prot     = prot;
    it->prot_end = offset + it->hdr->ih_protect_tlv_size;
    it->tlv_end  = offset + it->hdr->ih_protect_tlv_size + info.it_tlv_tot;
    // position on first TLV
    it->tlv_off = offset + sizeof(info);
    return 0;
}

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
file_tlv_iter_next(file_tlv_iter_t* const it, uint32_t* const off, uint16_t* const len, uint16_t* const type)
{
    if ((NULL == it) || (NULL == it->hdr) || (NULL == it->p_file))
    {
        return -1;
    }

    while (it->tlv_off < it->tlv_end)
    {
        if ((it->hdr->ih_protect_tlv_size > 0) && (it->tlv_off == it->prot_end))
        {
            it->tlv_off += sizeof(struct image_tlv_info);
        }

        struct image_tlv tlv = { 0 };
        zephyr_api_ret_t rc  = LOAD_IMAGE_DATA(it->hdr, it->p_file, it->tlv_off, (void*)&tlv, sizeof tlv);
        if (0 != rc)
        {
            return -1;
        }

        /* No more TLVs in the protected area */
        if (it->prot && (it->tlv_off >= it->prot_end))
        {
            return 1;
        }

        if ((IMAGE_TLV_ANY == it->type) || (tlv.it_type == it->type))
        {
            if (NULL != type)
            {
                *type = tlv.it_type;
            }
            *off = it->tlv_off + sizeof(tlv);
            *len = tlv.it_len;
            it->tlv_off += sizeof(tlv) + tlv.it_len;
            return 0;
        }

        it->tlv_off += sizeof(tlv) + tlv.it_len;
    }

    return 1;
}

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
file_tlv_iter_is_prot(const file_tlv_iter_t* const it, const uint32_t off)
{
    if ((NULL == it) || (NULL == it->hdr) || (NULL == it->p_file))
    {
        return -1;
    }

    return off < it->prot_end;
}
