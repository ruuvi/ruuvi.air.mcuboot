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
int
file_tlv_iter_begin(
    struct file_tlv_iter*      it,
    const struct image_header* hdr,
    struct fs_file_t* const    p_file,
    uint16_t                   type,
    bool                       prot)
{
    uint32_t              off_;
    struct image_tlv_info info;

    if (it == NULL || hdr == NULL || p_file == NULL)
    {
        return -1;
    }

    off_ = BOOT_TLV_OFF(hdr);
    if (LOAD_IMAGE_DATA(hdr, p_file, off_, (void*)&info, sizeof(info)))
    {
        return -1;
    }

    if (info.it_magic == IMAGE_TLV_PROT_INFO_MAGIC)
    {
        if (hdr->ih_protect_tlv_size != info.it_tlv_tot)
        {
            return -1;
        }

        if (LOAD_IMAGE_DATA(hdr, p_file, off_ + info.it_tlv_tot, (void*)&info, sizeof(info)))
        {
            return -1;
        }
    }
    else if (hdr->ih_protect_tlv_size != 0)
    {
        return -1;
    }

    if (info.it_magic != IMAGE_TLV_INFO_MAGIC)
    {
        return -1;
    }

    it->hdr      = hdr;
    it->p_file   = p_file;
    it->type     = type;
    it->prot     = prot;
    it->prot_end = off_ + it->hdr->ih_protect_tlv_size;
    it->tlv_end  = off_ + it->hdr->ih_protect_tlv_size + info.it_tlv_tot;
    // position on first TLV
    it->tlv_off = off_ + sizeof(info);
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
int
file_tlv_iter_next(struct file_tlv_iter* it, uint32_t* off, uint16_t* len, uint16_t* type)
{
    struct image_tlv tlv;
    int              rc;

    if (it == NULL || it->hdr == NULL || it->p_file == NULL)
    {
        return -1;
    }

    while (it->tlv_off < it->tlv_end)
    {
        if (it->hdr->ih_protect_tlv_size > 0 && it->tlv_off == it->prot_end)
        {
            it->tlv_off += sizeof(struct image_tlv_info);
        }

        rc = LOAD_IMAGE_DATA(it->hdr, it->p_file, it->tlv_off, (void*)&tlv, sizeof tlv);
        if (rc)
        {
            return -1;
        }

        /* No more TLVs in the protected area */
        if (it->prot && it->tlv_off >= it->prot_end)
        {
            return 1;
        }

        if (it->type == IMAGE_TLV_ANY || tlv.it_type == it->type)
        {
            if (type != NULL)
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
int
file_tlv_iter_is_prot(struct file_tlv_iter* it, uint32_t off)
{
    if (it == NULL || it->hdr == NULL || it->p_file == NULL)
    {
        return -1;
    }

    return off < it->prot_end;
}
