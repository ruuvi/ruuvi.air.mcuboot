/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mcuboot_fa_utils.h"
#include <stddef.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <sysflash/pm_sysflash.h>
#include <flash_map_backend/flash_map_backend.h>
#include <bootutil/bootutil_public.h>

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

/* s0/s1 package version of the current MCUboot image,
 * the values gets from SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION
 */
const struct image_version mcuboot_s0_s1_image_version = {
    .iv_major     = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MAJOR,
    .iv_minor     = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MINOR,
    .iv_revision  = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_REVISION,
    .iv_build_num = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_BUILD_NUMBER,
};

bool
get_flash_area_address_and_size(const fa_id_t fa_id, uint32_t* const p_fa_addr, uint32_t* const p_fa_size)
{
    const struct flash_area* p_fa = NULL;
    int32_t                  rc   = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        LOG_ERR("Failed to open flash area %d, rc=%d", fa_id, rc);
        return false;
    }
    *p_fa_addr = flash_area_get_off(p_fa);
    *p_fa_size = flash_area_get_size(p_fa);
    flash_area_close(p_fa);
    return true;
}

const char*
get_image_slot_name(const fa_id_t fa_id)
{
    switch (fa_id)
    {
        case FIXED_PARTITION_ID(provision):
            return "provision";
        case FIXED_PARTITION_ID(provision_ext):
            return "provision_ext";
        case FIXED_PARTITION_ID(s0):
            return "s0";
        case FIXED_PARTITION_ID(s0_ext):
            return "s0_ext";
        case FIXED_PARTITION_ID(s1):
            return "s1";
        case FIXED_PARTITION_ID(s1_ext):
            return "s1_ext";
        case FIXED_PARTITION_ID(mcuboot_primary):
            return "mcuboot_primary";
        case FIXED_PARTITION_ID(mcuboot_primary_ext):
            return "mcuboot_primary_ext";
        case FIXED_PARTITION_ID(mcuboot_secondary):
            return "mcuboot_secondary";
        case FIXED_PARTITION_ID(mcuboot_secondary_ext):
            return "mcuboot_secondary_ext";
        default:
            return "unknown";
    }
}

bool
load_image_header(const fa_id_t fa_id, struct image_header* const p_img_hdr)
{
    const struct flash_area* p_fa = NULL;
    int32_t                  rc   = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        return false;
    }
    rc = boot_image_load_header(p_fa, p_img_hdr);
    if (0 != rc)
    {
        flash_area_close(p_fa);
        return false;
    }
    flash_area_close(p_fa);
    return true;
}
