/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2017-2019 Linaro LTD
 * Copyright (c) 2016-2019 JUUL Labs
 * Copyright (c) 2019-2024 Arm Limited
 *
 * Original license:
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * This file is a port of original file: mcuboot/boot/bootutil/src/image_validate.c
 */

#include "file_img_validate.h"
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <flash_map_backend/flash_map_backend.h>

#include "bootutil/image.h"
#include "bootutil/crypto/sha.h"
#include "bootutil/sign_key.h"
#include "bootutil/security_cnt.h"
#include "bootutil/fault_injection_hardening.h"

#include "mcuboot_config/mcuboot_config.h"

#if defined(MCUBOOT_SIGN_RSA)
#include "mbedtls/rsa.h"
#endif
#if defined(MCUBOOT_SIGN_EC256)
#include "mbedtls/ecdsa.h"
#endif
#if defined(MCUBOOT_SIGN_RSA) || defined(MCUBOOT_SIGN_EC256)
#include "mbedtls/asn1.h"
#endif

#include "file_tlv_priv.h"

#include "file_tlv.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#ifndef MCUBOOT_SIGN_PURE
/*
 * Compute SHA hash over the image.
 * (SHA384 if ECDSA-P384 is being used,
 *  SHA256 otherwise).
 */
static zephyr_api_ret_t
file_img_hash(
    const struct image_header* const hdr,
    struct fs_file_t* const          p_file,
    uint8_t* const                   tmp_buf,
    const uint32_t                   tmp_buf_sz,
    uint8_t* const                   hash_result,
    const uint8_t* const             seed,
    const ssize_t                    seed_len)
{
    bootutil_sha_context sha_ctx;
    uint32_t             size;
    uint16_t             hdr_size;
    uint32_t             blk_off;
    uint32_t             tlv_off;
    uint32_t             blk_sz;

    (void)hdr_size;
    (void)blk_off;
    (void)tlv_off;

    bootutil_sha_init(&sha_ctx);

    /* in some cases (split image) the hash is seeded with data from
     * the loader image */
    if ((NULL != seed) && (seed_len > 0))
    {
        bootutil_sha_update(&sha_ctx, seed, seed_len);
    }

    /* Hash is computed over image header and image itself. */
    size = hdr->ih_hdr_size + hdr->ih_img_size;

    /* If protected TLVs are present they are also hashed. */
    size += hdr->ih_protect_tlv_size;

    uint32_t off = 0;
    while (off < size)
    {
        blk_sz = size - off;
        if (blk_sz > tmp_buf_sz)
        {
            blk_sz = tmp_buf_sz;
        }
        const zephyr_api_ret_t rc = load_image_data(p_file, off, tmp_buf, blk_sz);
        if (0 != rc)
        {
            bootutil_sha_drop(&sha_ctx);
            return rc;
        }
        bootutil_sha_update(&sha_ctx, tmp_buf, blk_sz);
        off += blk_sz;
    }
    bootutil_sha_finish(&sha_ctx, hash_result);
    bootutil_sha_drop(&sha_ctx);

    return 0;
}
#endif

/*
 * Currently, we only support being able to verify one type of
 * signature, because there is a single verification function that we
 * call.  List the type of TLV we are expecting.  If we aren't
 * configured for any signature, don't define this macro.
 */
#if ( \
    defined(MCUBOOT_SIGN_RSA) + defined(MCUBOOT_SIGN_EC256) + defined(MCUBOOT_SIGN_EC384) \
    + defined(MCUBOOT_SIGN_ED25519)) \
    > 1
#error "Only a single signature type is supported!"
#endif

#if defined(MCUBOOT_SIGN_RSA)
#if MCUBOOT_SIGN_RSA_LEN == 2048
#define EXPECTED_SIG_TLV IMAGE_TLV_RSA2048_PSS
#elif MCUBOOT_SIGN_RSA_LEN == 3072
#define EXPECTED_SIG_TLV IMAGE_TLV_RSA3072_PSS
#else
#error "Unsupported RSA signature length"
#endif
#define SIG_BUF_SIZE        (MCUBOOT_SIGN_RSA_LEN / 8)
#define EXPECTED_SIG_LEN(x) ((x) == SIG_BUF_SIZE) /* NOSONAR: 2048 bits */
#elif defined(MCUBOOT_SIGN_EC256) || defined(MCUBOOT_SIGN_EC384) || defined(MCUBOOT_SIGN_EC)
#define EXPECTED_SIG_TLV    IMAGE_TLV_ECDSA_SIG
#define SIG_BUF_SIZE        128
#define EXPECTED_SIG_LEN(x) (1) /* NOSONAR: always true, ASN.1 will validate */
#elif defined(MCUBOOT_SIGN_ED25519)
#define EXPECTED_SIG_TLV    IMAGE_TLV_ED25519
#define SIG_BUF_SIZE        64
#define EXPECTED_SIG_LEN(x) ((x) == SIG_BUF_SIZE) /* NOSONAR */
#else
#define SIG_BUF_SIZE 32 /* no signing, sha256 digest only */
#endif

#if (defined(MCUBOOT_HW_KEY) + defined(MCUBOOT_BUILTIN_KEY)) > 1
#error "Please use either MCUBOOT_HW_KEY or the MCUBOOT_BUILTIN_KEY feature."
#endif

#ifdef EXPECTED_SIG_TLV

#if !defined(MCUBOOT_BUILTIN_KEY)
#if !defined(MCUBOOT_HW_KEY)
/* The key TLV contains the hash of the public key. */
#define EXPECTED_KEY_TLV IMAGE_TLV_KEYHASH
#define KEY_BUF_SIZE     IMAGE_HASH_SIZE
#else
/* The key TLV contains the whole public key.
 * Add a few extra bytes to the key buffer size for encoding and
 * for public exponent.
 */
#define EXPECTED_KEY_TLV IMAGE_TLV_PUBKEY
#define KEY_BUF_SIZE     (SIG_BUF_SIZE + 24)
#endif /* !MCUBOOT_HW_KEY */

#if !defined(CONFIG_BOOT_SIGNATURE_USING_KMU)
#if !defined(MCUBOOT_HW_KEY)
static int32_t
file_img_find_key(const uint8_t* const keyhash, const uint8_t keyhash_len)
{
    bootutil_sha_context sha_ctx = { 0 };
    uint8_t              hash[IMAGE_HASH_SIZE];

    if (keyhash_len > IMAGE_HASH_SIZE)
    {
        return -1;
    }

    for (int32_t i = 0; i < bootutil_key_cnt; ++i)
    {
        const struct bootutil_key* key = &bootutil_keys[i];
        bootutil_sha_init(&sha_ctx);
        bootutil_sha_update(&sha_ctx, key->key, *key->len);
        bootutil_sha_finish(&sha_ctx, hash);
        if (0 == memcmp(hash, keyhash, keyhash_len))
        {
            bootutil_sha_drop(&sha_ctx);
            return i;
        }
    }
    bootutil_sha_drop(&sha_ctx);
    return -1;
}
#else  /* !MCUBOOT_HW_KEY */
extern unsigned int pub_key_len;
static int32_t
file_img_find_key(uint8_t image_index, uint8_t* key, uint16_t key_len)
{
    bootutil_sha_context sha_ctx = { 0 };
    uint8_t              hash[IMAGE_HASH_SIZE];
    uint8_t              key_hash[IMAGE_HASH_SIZE];
    size_t               key_hash_size = sizeof(key_hash);
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    bootutil_sha_init(&sha_ctx);
    bootutil_sha_update(&sha_ctx, key, key_len);
    bootutil_sha_finish(&sha_ctx, hash);
    bootutil_sha_drop(&sha_ctx);

    int rc = boot_retrieve_public_key_hash(image_index, key_hash, &key_hash_size);
    if (rc)
    {
        return -1;
    }

    /* Adding hardening to avoid this potential attack:
     *  - Image is signed with an arbitrary key and the corresponding public
     *    key is added as a TLV field.
     * - During public key validation (comparing against key-hash read from
     *   HW) a fault is injected to accept the public key as valid one.
     */
    FIH_CALL(boot_fih_memequal, fih_rc, hash, key_hash, key_hash_size);
    if (FIH_EQ(fih_rc, FIH_SUCCESS))
    {
        bootutil_keys[0].key = key;
        pub_key_len          = key_len;
        return 0;
    }

    return -1;
}
#endif /* !MCUBOOT_HW_KEY */
#endif /* !MCUBOOT_BUILTIN_KEY */
#endif /* !defined(CONFIG_BOOT_SIGNATURE_USING_KMU) */
#endif /* EXPECTED_SIG_TLV */

/**
 * Reads the value of an image's security counter.
 *
 * @param hdr           Pointer to the image header structure.
 * @param fap           Pointer to a description structure of the image's
 *                      flash area.
 * @param security_cnt  Pointer to store the security counter value.
 *
 * @return              0 on success; nonzero on failure.
 */
int32_t
file_img_get_security_cnt(
    const struct image_header* const hdr,
    struct fs_file_t* const          p_file,
    uint32_t* const                  img_security_cnt)
{
    file_tlv_iter_t it = { 0 };

    if ((NULL == hdr) || (NULL == p_file) || (NULL == img_security_cnt))
    {
        /* Invalid parameter. */
        return BOOT_EBADARGS;
    }

    /* The security counter TLV is in the protected part of the TLV area. */
    if (0 == hdr->ih_protect_tlv_size)
    {
        return BOOT_EBADIMAGE;
    }

    zephyr_api_ret_t rc = file_tlv_iter_begin(&it, hdr, p_file, IMAGE_TLV_SEC_CNT, true);
    if (0 != rc)
    {
        return rc;
    }

    /* Traverse through the protected TLV area to find
     * the security counter TLV.
     */

    uint32_t off = 0;
    uint16_t len = 0;
    rc           = file_tlv_iter_next(&it, &off, &len, NULL);
    if (0 != rc)
    {
        /* Security counter TLV has not been found. */
        return -1;
    }

    if (len != sizeof(*img_security_cnt))
    {
        /* Security counter is not valid. */
        return BOOT_EBADIMAGE;
    }

    rc = LOAD_IMAGE_DATA(hdr, p_file, off, (void*)img_security_cnt, len);
    if (0 != rc)
    {
        return BOOT_EFLASH;
    }

    return 0;
}

#if defined(MCUBOOT_SIGN_PURE)
/* Returns:
 *  0 -- found
 *  1 -- not found or found but not true
 * -1 -- failed for some reason
 *
 * Value of TLV does not matter, presence decides.
 */
static int
file_img_check_for_pure(const struct image_header* hdr, struct fs_file_t* const p_file)
{
    file_tlv_iter_t it { 0 };

    zephyr_api_ret_t rc = file_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_SIG_PURE, false);
    if (rc)
    {
        return rc;
    }

    /* Search for the TLV */
    uint32_t off = 0;
    uint16_t len = 0;
    rc           = file_tlv_iter_next(&it, &off, &len, NULL);
    if ((0 == rc) && (1 == len))
    {
        bool val = false;
        rc       = LOAD_IMAGE_DATA(hdr, fap, off, &val, 1);
        if (0 == rc)
        {
            rc = !val;
        }
    }

    return rc;
}
#endif

#ifndef ALLOW_ROGUE_TLVS
/*
 * The following list of TLVs are the only entries allowed in the unprotected
 * TLV section.  All other TLV entries must be in the protected section.
 */
static const uint16_t allowed_unprot_tlvs[] /* NOSONAR */ = {
    IMAGE_TLV_KEYHASH,
    IMAGE_TLV_PUBKEY,
    IMAGE_TLV_SHA256,
    IMAGE_TLV_SHA384,
    IMAGE_TLV_SHA512,
    IMAGE_TLV_RSA2048_PSS,
    IMAGE_TLV_ECDSA224,
    IMAGE_TLV_ECDSA_SIG,
    IMAGE_TLV_RSA3072_PSS,
    IMAGE_TLV_ED25519,
#if defined(MCUBOOT_SIGN_PURE)
    IMAGE_TLV_SIG_PURE,
#endif
    IMAGE_TLV_ENC_RSA2048,
    IMAGE_TLV_ENC_KW,
    IMAGE_TLV_ENC_EC256,
    IMAGE_TLV_ENC_X25519,
    /* Mark end with ANY. */
    IMAGE_TLV_ANY,
};
#endif

static inline bool
file_image_validate_tlv_expected_hash(
    struct fs_file_t* const p_file,
    const uint32_t          off,
    const uint16_t          len,
    const uint8_t* const    p_hash,
    bool* const             p_image_hash_valid)
{
    uint8_t buf[IMAGE_HASH_SIZE];
    /* Verify the image hash. This must always be present. */
    if (IMAGE_HASH_SIZE != len)
    {
        return false;
    }
    zephyr_api_ret_t rc = LOAD_IMAGE_DATA(hdr, p_file, off, buf, IMAGE_HASH_SIZE);
    if (0 != rc)
    {
        return false;
    }

    FIH_DECLARE(fih_rc, FIH_FAILURE);
    FIH_CALL(boot_fih_memequal, fih_rc, p_hash, buf, IMAGE_HASH_SIZE);
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS))
    {
        return false;
    }

    *p_image_hash_valid = true;
    LOG_INF("EXPECTED_HASH_TLV: hash is valid");
    return true;
}

static inline bool
file_image_validate_tlv_expected_key(
    struct fs_file_t* const p_file,
    const uint32_t          off,
    const uint16_t          len,
    int32_t* const          p_key_id)
{
    uint8_t buf[KEY_BUF_SIZE];
#ifdef MCUBOOT_HW_KEY
    uint8_t key_buf[KEY_BUF_SIZE];
#endif
    /*
     * Determine which key we should be checking.
     */
    if (len > KEY_BUF_SIZE)
    {
        return false;
    }
#ifndef MCUBOOT_HW_KEY
    zephyr_api_ret_t rc = LOAD_IMAGE_DATA(hdr, p_file, off, buf, len);
    if (0 != rc)
    {
        return false;
    }
    *p_key_id = file_img_find_key(buf, (uint8_t)len);
#else
    zephyr_api_ret_t rc = LOAD_IMAGE_DATA(hdr, fap, off, key_buf, len);
    if (0 != rc)
    {
        return false;
    }
    *p_key_id = file_img_find_key(image_index, key_buf, len);
#endif /* !MCUBOOT_HW_KEY */
    /*
     * The key may not be found, which is acceptable.  There
     * can be multiple signatures, each preceded by a key.
     */
    LOG_INF("EXPECTED_KEY_TLV: key_id=%" PRId32, *p_key_id);
    return true;
}

static inline bool
file_image_validate_tlv_expected_sig(
    struct fs_file_t* const p_file,
    const uint32_t          off,
    const uint16_t          len,
    uint8_t* const          p_hash,
    fih_ret* const          p_valid_signature,
    int32_t* const          p_key_id)
{
    uint8_t buf[SIG_BUF_SIZE];
    if ((0 == EXPECTED_SIG_LEN(len)) || (len > sizeof(buf)))
    {
        LOG_ERR("EXPECTED_SIG_TLV: invalid signature length: %u", len);
        return false;
    }
    zephyr_api_ret_t rc = LOAD_IMAGE_DATA(hdr, p_file, off, buf, len);
    if (0 != rc)
    {
        LOG_ERR("EXPECTED_SIG_TLV: failed to load signature data, rc=%d", rc);
        return false;
    }
#ifndef MCUBOOT_SIGN_PURE
    FIH_CALL(bootutil_verify_sig, *p_valid_signature, p_hash, IMAGE_HASH_SIZE, buf, len, (uint8_t)*p_key_id);
#else
    /* Directly check signature on the image, by using the mapping of
     * a device to memory. The pointer is beginning of image in flash,
     * so offset of area, the range is header + image + protected tlvs.
     */
    FIH_CALL(
        bootutil_verify_img,
        *p_valid_signature,
        (void*)flash_area_get_off(fap),
        hdr->ih_hdr_size + hdr->ih_img_size + hdr->ih_protect_tlv_size,
        buf,
        len,
        (uint8_t)*p_key_id);
#endif
    *p_key_id = -1;
    LOG_INF(
        "EXPECTED_SIG_TLV: signature verification result: %s",
        FIH_EQ(*p_valid_signature, FIH_SUCCESS) ? "OK" : "FAIL");

    return true;
}

static inline zephyr_api_ret_t
file_image_validate_tlv(
    struct fs_file_t* const p_file,
    file_tlv_iter_t* const  p_it,
    uint8_t* const          p_hash,
    int32_t* const          p_key_id,
    bool* const             p_image_hash_valid,
    fih_ret* const          p_valid_signature)
{
    uint32_t off  = 0;
    uint16_t len  = 0;
    uint16_t type = 0;

    zephyr_api_ret_t rc = file_tlv_iter_next(p_it, &off, &len, &type);
    if (0 != rc)
    {
        return rc;
    }

#ifndef ALLOW_ROGUE_TLVS
    /*
     * Ensure that the non-protected TLV only has entries necessary to hold
     * the signature.  We also allow encryption related keys to be in the
     * unprotected area.
     */
    if (0 == file_tlv_iter_is_prot(p_it, off))
    {
        bool found = false;
        for (const uint16_t* p = allowed_unprot_tlvs; *p != IMAGE_TLV_ANY; ++p)
        {
            if (type == *p)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return -1;
        }
    }
#endif
    switch (type)
    {
#if defined(EXPECTED_HASH_TLV) && !defined(MCUBOOT_SIGN_PURE)
        case EXPECTED_HASH_TLV:
        {
            LOG_INF("Handle record: EXPECTED_HASH_TLV");
            if (!file_image_validate_tlv_expected_hash(p_file, off, len, p_hash, p_image_hash_valid))
            {
                return -1;
            }
            break;
        }
#endif /* defined(EXPECTED_HASH_TLV) && !defined(MCUBOOT_SIGN_PURE) */
#if !defined(CONFIG_BOOT_SIGNATURE_USING_KMU)
#ifdef EXPECTED_KEY_TLV
        case EXPECTED_KEY_TLV:
        {
            LOG_INF("Handle record: EXPECTED_KEY_TLV");
            if (!file_image_validate_tlv_expected_key(p_file, off, len, p_key_id))
            {
                return -1;
            }
            break;
        }
#endif /* EXPECTED_KEY_TLV */
#endif /* !defined(CONFIG_BOOT_SIGNATURE_USING_KMU) */
#ifdef EXPECTED_SIG_TLV
        case EXPECTED_SIG_TLV:
        {
            LOG_INF("Handle record: EXPECTED_SIG_TLV");

#if !defined(CONFIG_BOOT_SIGNATURE_USING_KMU)
            /* Ignore this signature if it is out of bounds. */
            if ((*p_key_id < 0) || (*p_key_id >= bootutil_key_cnt))
            {
                LOG_WRN("Invalid key_id=%" PRId32 ", skipping", *p_key_id);
                *p_key_id = -1;
                return 1; /* try next TLV */
            }
#endif /* !defined(CONFIG_BOOT_SIGNATURE_USING_KMU) */

            if (!file_image_validate_tlv_expected_sig(p_file, off, len, p_hash, p_valid_signature, p_key_id))
            {
                return -1;
            }
            break;
        }
        default:
            /* Ignore other TLV types. */
            break;
#endif /* EXPECTED_SIG_TLV */
    }
    return 0;
}

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
    const ssize_t                    seed_len)
{
#ifdef EXPECTED_SIG_TLV
    FIH_DECLARE(valid_signature, FIH_FAILURE);
#endif /* EXPECTED_SIG_TLV */
    file_tlv_iter_t it = { 0 };
#if defined(EXPECTED_HASH_TLV) && !defined(MCUBOOT_SIGN_PURE)
    bool    image_hash_valid = false;
    uint8_t hash[IMAGE_HASH_SIZE];
#endif
    zephyr_api_ret_t rc = 0;
    FIH_DECLARE(fih_rc, FIH_FAILURE);
#ifdef MCUBOOT_HW_ROLLBACK_PROT
    fih_int  security_cnt     = fih_int_encode(INT_MAX);
    uint32_t img_security_cnt = 0;
    FIH_DECLARE(security_counter_valid, FIH_FAILURE);
#endif

#if defined(EXPECTED_HASH_TLV) && !defined(MCUBOOT_SIGN_PURE)
    rc = file_img_hash(hdr, p_file, tmp_buf, tmp_buf_sz, hash, seed, seed_len);
    if (0 != rc)
    {
        goto OUT; // NOSONAR
    }
#endif

#if defined(MCUBOOT_SIGN_PURE)
    /* If Pure type signature is expected then it has to be there */
    rc = file_img_check_for_pure(hdr, p_file);
    if (0 != rc)
    {
        goto OUT; // NOSONAR
    }
#endif

    rc = file_tlv_iter_begin(&it, hdr, p_file, IMAGE_TLV_ANY, false);
    if (0 != rc)
    {
        goto OUT; // NOSONAR
    }

    if (it.tlv_end > fa_size)
    {
        rc = -1;
        goto OUT; // NOSONAR
    }

#ifdef EXPECTED_SIG_TLV
#ifndef MCUBOOT_BUILTIN_KEY
    int32_t key_id = -1;
#else
    /* Pass a key ID equal to the image index, the underlying crypto library
     * is responsible for mapping the image index to a builtin key ID.
     */
    int32_t key_id = image_index;
#endif /* !MCUBOOT_BUILTIN_KEY */
#endif /* EXPECTED_SIG_TLV */

    /*
     * Traverse through all of the TLVs, performing any checks we know
     * and are able to do.
     */
    while (true)
    {
        rc = file_image_validate_tlv(p_file, &it, hash, &key_id, &image_hash_valid, &valid_signature);
        if (rc < 0)
        {
            goto OUT; // NOSONAR
        }
        if (rc > 0)
        {
            break;
        }
    }

#if defined(EXPECTED_HASH_TLV) && !defined(MCUBOOT_SIGN_PURE)
    rc = (!image_hash_valid) ? -1 : 0;
    if (0 != rc)
    {
        goto OUT; // NOSONAR
    }
#elif defined(MCUBOOT_SIGN_PURE)
    /* This returns true on EQ, rc is err on non-0 */
    rc = FIH_NOT_EQ(valid_signature, FIH_SUCCESS);
#endif
#ifdef MCUBOOT_HW_ROLLBACK_PROT
    if (FIH_NOT_EQ(security_counter_valid, FIH_SUCCESS))
    {
        rc = -1;
        goto OUT; // NOSONAR
    }
#endif

#ifdef EXPECTED_SIG_TLV
    FIH_SET(fih_rc, valid_signature);
#endif

OUT:
    if (0 != rc)
    {
        FIH_SET(fih_rc, FIH_FAILURE);
    }

    FIH_RET(fih_rc);
}
