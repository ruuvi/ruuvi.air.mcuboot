/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/app_version.h>
#include <sysflash/pm_sysflash.h>
#include <bootutil/mcuboot_status.h>
#include <bootutil/bootutil.h>
#include <bootutil/boot_status.h>
#include <bootutil/boot_record.h>
#include <fw_info.h>
#include "mcuboot_fw_update.h"
#include "mcuboot_fa_utils.h"
#include "mcuboot_segger_rtt.h"
#include "mcuboot_version.h"
#include "app_version.h"
#include "ncs_version.h"
#include "version.h"
#include "app_commit.h"
#include "ncs_commit.h"
#include "zephyr_commit.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#if USE_PARTITION_MANAGER && CONFIG_FPROTECT
#ifdef PM_S1_ADDRESS
/* MCUBoot is stored in either S0 or S1, protect both */
#define PROTECT_SIZE (PM_MCUBOOT_PRIMARY_ADDRESS - PM_S0_ADDRESS)
#define PROTECT_ADDR PM_S0_ADDRESS
#else
/* There is only one instance of MCUBoot */
#define PROTECT_SIZE (PM_MCUBOOT_PRIMARY_ADDRESS - PM_MCUBOOT_ADDRESS)
#define PROTECT_ADDR PM_MCUBOOT_ADDRESS
#endif
#endif

const int32_t g_cfg_hw_rev =
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    1
#elif defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_2)
    2
#else
    0
#endif
    ;

static uint32_t
app_max_size(const fa_id_t fa_id1, const fa_id_t fa_id2)
{
    const struct flash_area* p_fa = NULL;

    zephyr_api_ret_t rc = flash_area_open(fa_id1, &p_fa);
    if (0 != rc)
    {
        return 0;
    }
    const uint32_t primary_sz = flash_area_get_size(p_fa);
    flash_area_close(p_fa);

    rc = flash_area_open(fa_id2, &p_fa);
    if (0 != rc)
    {
        return 0;
    }
    const uint32_t secondary_sz = flash_area_get_size(p_fa);
    flash_area_close(p_fa);

    return (secondary_sz < primary_sz) ? secondary_sz : primary_sz;
}

static bool
boot_add_shared_data(const slot_id_t active_slot, const fa_id_t active_fa_id, const struct flash_area* const p_fa)
{
#if defined(MCUBOOT_MEASURED_BOOT) || defined(MCUBOOT_DATA_SHARING)
    zephyr_api_ret_t rc = 0;

    struct image_header img_hdr = { 0 };
    if (!load_image_header(active_fa_id, &img_hdr))
    {
        LOG_ERR("Failed to load image header for active slot fa_id=%d", active_fa_id);
        return false;
    }

#ifdef MCUBOOT_MEASURED_BOOT
    rc = boot_save_boot_status(0 /* TODO: need to use actual value for 'sw_module' instead of 0 */, &img_hdr, p_fa);
    if (rc != 0)
    {
        LOG_ERR("Failed to add image data to shared area");
    }
#endif /* MCUBOOT_MEASURED_BOOT */

    const struct image_max_size max_app_sizes[BOOT_IMAGE_NUMBER] = {
        [0] = {
            .calculated = true,
            .max_size = app_max_size((fa_id_t)FLASH_AREA_IMAGE_PRIMARY(0), (fa_id_t)FLASH_AREA_IMAGE_SECONDARY(0)),
        },
        [CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] = {
            .calculated = true,
            .max_size = app_max_size(
                // Use the primary slot only in both cases.
                // When primary bootloader is active, the 's1' slot is used as primary,
                // and when secondary bootloader is active, the 's0' slot is used as primary,
                // and in both cases 'fw_loader' is in the secondary slot.
                (fa_id_t)FLASH_AREA_IMAGE_PRIMARY(CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER),
                (fa_id_t)FLASH_AREA_IMAGE_PRIMARY(CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER)),
        },
    };
#ifdef MCUBOOT_DATA_SHARING
    rc = boot_save_shared_data(&img_hdr, p_fa, active_slot, max_app_sizes);
    if (rc != 0)
    {
        LOG_ERR("Failed to add data to shared memory area.");
        return false;
    }
#endif /* MCUBOOT_DATA_SHARING */

#else /* MCUBOOT_MEASURED_BOOT || MCUBOOT_DATA_SHARING */
    (void)(state);
    (void)(active_slot);
    (void)(p_fa);
#endif
    return true;
}

static void
save_shared_data_for_active_slot(slot_id_t mcuboot_active_slot, const fa_id_t mcuboot_active_fa_id)
{
    const struct flash_area* p_fa = NULL;
    int32_t                  rc   = flash_area_open(mcuboot_active_fa_id, &p_fa);
    if (0 != rc)
    {
        LOG_ERR("%s: Failed to open flash area %d, rc=%d", __func__, mcuboot_active_fa_id, rc);
        return;
    }
    LOG_INF("Save shared data for active mcuboot slot %d", mcuboot_active_slot);
    if (!boot_add_shared_data(mcuboot_active_slot, mcuboot_active_fa_id, p_fa))
    {
        LOG_ERR("Failed to save shared data for active mcuboot slot %d", mcuboot_active_slot);
    }
    flash_area_close(p_fa);
}

static void
print_image_info(const fa_id_t fa_id, fw_image_hw_rev_t* const p_hw_rev)
{
    uint32_t fa_addr = 0;
    uint32_t fa_size = 0;
    if (!get_flash_area_address_and_size(fa_id, &fa_addr, &fa_size))
    {
        LOG_ERR("Failed to get flash area address and size for %d (%s)", fa_id, get_image_slot_name(fa_id));
        return;
    }

    const struct fw_info* const p_fw_info = fw_info_find(fa_addr);
    if (NULL == p_fw_info)
    {
        LOG_ERR("Failed to find fw_info for flash area %d (%s)", fa_id, get_image_slot_name(fa_id));
        return;
    }

    struct image_header img_hdr = { 0 };
    if (!load_image_header(fa_id, &img_hdr))
    {
        LOG_ERR("Failed to load image header for flash area %d (%s)", fa_id, get_image_slot_name(fa_id));
        return;
    }
    fw_image_hw_rev_t hw_rev = { 0 };
    if (!fw_img_hw_rev_find_in_flash_area(fa_id, &hw_rev))
    {
        LOG_WRN("Image in flash area %d (%s): No Ruuvi HW revision TLVs found", fa_id, get_image_slot_name(fa_id));
    }
    LOG_INF(
        "### Flash area %d (%s): Image version: v%u.%u.%u+%u, FwInfoVer: %u, HwRev: ID=%" PRIu32 ", name='%s' ###",
        fa_id,
        get_image_slot_name(fa_id),
        img_hdr.ih_ver.iv_major,
        img_hdr.ih_ver.iv_minor,
        img_hdr.ih_ver.iv_revision,
        img_hdr.ih_ver.iv_build_num,
        p_fw_info->version,
        hw_rev.hw_rev_num,
        hw_rev.hw_rev_name);
    if (NULL != p_hw_rev)
    {
        *p_hw_rev = hw_rev;
    }
}

static void
on_startup_print_logs(void)
{
    LOG_INF(
        "### Ruuvi MCUboot: Image Version: v%u.%u.%u+%u (FwInfoCnt: %u)",
        mcuboot_s0_s1_image_version.iv_major,
        mcuboot_s0_s1_image_version.iv_minor,
        mcuboot_s0_s1_image_version.iv_revision,
        mcuboot_s0_s1_image_version.iv_build_num,
        CONFIG_FW_INFO_FIRMWARE_VERSION);
    LOG_INF(
        "### Ruuvi MCUboot: Version: %s, build: %s",
        MCUBOOT_VERSION_EXTENDED_STRING,
        STRINGIFY(MCUBOOT_BUILD_VERSION));
    LOG_INF(
        "### Based on MCUboot: Version: %s, build: %s, commit: %s",
        APP_VERSION_EXTENDED_STRING,
        STRINGIFY(APP_BUILD_VERSION),
        APP_COMMIT_STRING);
    LOG_INF(
        "### MCUboot: NCS version: %s, build: %s, commit: %s",
        NCS_VERSION_STRING,
        STRINGIFY(NCS_BUILD_VERSION),
        NCS_COMMIT_STRING);
    LOG_INF(
        "### MCUboot: Kernel version: %s, build: %s, commit: %s",
        KERNEL_VERSION_EXTENDED_STRING,
        STRINGIFY(BUILD_VERSION),
        ZEPHYR_COMMIT_STRING);
}

static void
on_startup_print_slots_info_and_get_hw_rev(
    const slot_id_t          mcuboot_active_slot,
    const fa_id_t            mcuboot_active_fa_id,
    fw_image_hw_rev_t* const p_hw_rev)
{
    LOG_INF(
        "### MCUboot: Active slot: %s (%s), id=%d",
        (0 == mcuboot_active_slot) ? "primary" : "secondary",
        (0 == mcuboot_active_slot) ? "s0" : "s1",
        mcuboot_active_fa_id);

    LOG_INF("### MCUboot: primary area id=%d", FLASH_AREA_IMAGE_PRIMARY(0));
    LOG_INF("### MCUboot: secondary area id=%d", FLASH_AREA_IMAGE_SECONDARY(0));

    print_image_info(PM_ID(s0), (0 == mcuboot_active_slot) ? p_hw_rev : NULL);
    print_image_info(PM_ID(s1), (0 != mcuboot_active_slot) ? p_hw_rev : NULL);
    print_image_info(PM_ID(mcuboot_primary), NULL);
    print_image_info(PM_ID(mcuboot_secondary), NULL);
}

static void
on_startup(void)
{
    on_startup_print_logs();
    mcuboot_segger_rtt_check_data_location_and_size();

#if 0
    LOG_INF("Seeep 10 seconds...");
    k_msleep(10000);
    LOG_INF("Wake up");
#endif

#ifndef CONFIG_NCS_IS_VARIANT_IMAGE
    const slot_id_t mcuboot_active_slot = 0;
#else
    const slot_id_t mcuboot_active_slot = 1;
#endif

    const fa_id_t mcuboot_active_fa_id = (0 == mcuboot_active_slot) ? PM_ID(s0) : PM_ID(s1);

    fw_image_hw_rev_t hw_rev = { 0 };
    on_startup_print_slots_info_and_get_hw_rev(mcuboot_active_slot, mcuboot_active_fa_id, &hw_rev);

    if (hw_rev.hw_rev_num != g_cfg_hw_rev)
    {
        LOG_ERR("Hardware revision mismatch: fw image hw_rev_id: %d, Kconfig: %d", hw_rev.hw_rev_num, g_cfg_hw_rev);
        __ASSERT(
            hw_rev.hw_rev_num == g_cfg_hw_rev,
            "Hardware revision mismatch: fw image hw_rev_id: %d, Kconfig: %d",
            hw_rev.hw_rev_id,
            g_cfg_hw_rev);
        while (1)
        {
            k_msleep(1000); // NOSONAR: wait forever
        }
    }

    struct image_header img_hdr = { 0 };
    if (!load_image_header((0 == mcuboot_active_slot) ? PM_ID(s0) : PM_ID(s1), &img_hdr))
    {
        LOG_ERR("Failed to load image header for flash area %d", (0 == mcuboot_active_slot) ? PM_ID(s0) : PM_ID(s1));
        return;
    }
    if ((img_hdr.ih_ver.iv_major != mcuboot_s0_s1_image_version.iv_major)
        || (img_hdr.ih_ver.iv_minor != mcuboot_s0_s1_image_version.iv_minor)
        || (img_hdr.ih_ver.iv_revision != mcuboot_s0_s1_image_version.iv_revision)
        || (img_hdr.ih_ver.iv_build_num != mcuboot_s0_s1_image_version.iv_build_num))
    {
        LOG_ERR(
            "MCUboot version mismatch: image version: v%u.%u.%u+%u, expected version: v%u.%u.%u+%u",
            img_hdr.ih_ver.iv_major,
            img_hdr.ih_ver.iv_minor,
            img_hdr.ih_ver.iv_revision,
            img_hdr.ih_ver.iv_build_num,
            mcuboot_s0_s1_image_version.iv_major,
            mcuboot_s0_s1_image_version.iv_minor,
            mcuboot_s0_s1_image_version.iv_revision,
            mcuboot_s0_s1_image_version.iv_build_num);
        __ASSERT(
            (img_hdr.ih_ver.iv_major == mcuboot_s0_s1_image_version.iv_major)
                && (img_hdr.ih_ver.iv_minor == mcuboot_s0_s1_image_version.iv_minor)
                && (img_hdr.ih_ver.iv_revision == mcuboot_s0_s1_image_version.iv_revision)
                && (img_hdr.ih_ver.iv_build_num == mcuboot_s0_s1_image_version.iv_build_num),
            "MCUboot version mismatch: image version: v%u.%u.%u+%u, expected version: v%u.%u.%u+%u",
            img_hdr.ih_ver.iv_major,
            img_hdr.ih_ver.iv_minor,
            img_hdr.ih_ver.iv_revision,
            img_hdr.ih_ver.iv_build_num,
            mcuboot_s0_s1_image_version.iv_major,
            mcuboot_s0_s1_image_version.iv_minor,
            mcuboot_s0_s1_image_version.iv_revision,
            mcuboot_s0_s1_image_version.iv_build_num);
        while (1)
        {
            k_msleep(1000); // NOSONAR: wait forever
        }
    }

    char expected_version_str[32];
    snprintf(
        expected_version_str,
        sizeof(expected_version_str),
        "%u.%u.%u+%" PRIu32,
        mcuboot_s0_s1_image_version.iv_major,
        mcuboot_s0_s1_image_version.iv_minor,
        mcuboot_s0_s1_image_version.iv_revision,
        mcuboot_s0_s1_image_version.iv_build_num);
    if (0 != strcmp(expected_version_str, MCUBOOT_VERSION_TWEAK_STRING))
    {
        LOG_ERR(
            "Image version mismatch: fw image: %s, App Version: %s",
            expected_version_str,
            MCUBOOT_VERSION_TWEAK_STRING);
        __ASSERT(
            0 == strcmp(expected_version_str, MCUBOOT_VERSION_TWEAK_STRING),
            "Image version mismatch: fw image: %s, App Version: %s",
            expected_version_str,
            MCUBOOT_VERSION_TWEAK_STRING);
        while (1)
        {
            k_msleep(1000); // NOSONAR: wait forever
        }
    }

    mcuboot_fw_update(mcuboot_active_slot, &hw_rev);

    save_shared_data_for_active_slot(mcuboot_active_slot, mcuboot_active_fa_id);

#if defined(CONFIG_USE_SEGGER_RTT)
    k_msleep(500); // NOSONAR: wait for log to be flushed
#endif
}

static void
on_bootable_image_found(void)
{
    LOG_INF("### MCUboot status: %s", "BOOTABLE_IMAGE_FOUND");
#if USE_PARTITION_MANAGER && CONFIG_FPROTECT
    LOG_INF("Protecting MCUBoot flash area, address: 0x%x, size: 0x%x", PROTECT_ADDR, PROTECT_SIZE);
#endif
    k_msleep(100); // NOSONAR
}

void
mcuboot_status_change(mcuboot_status_type_t status)
{
    switch (status)
    {
        case MCUBOOT_STATUS_STARTUP:
            on_startup();
            break;
        case MCUBOOT_STATUS_UPGRADING:
            LOG_INF("### MCUboot status: %s", "UPGRADING");
            break;
        case MCUBOOT_STATUS_BOOTABLE_IMAGE_FOUND:
            on_bootable_image_found();
            break;
        case MCUBOOT_STATUS_NO_BOOTABLE_IMAGE_FOUND:
            LOG_ERR("### MCUboot status: %s", "NO_BOOTABLE_IMAGE_FOUND");
            break;
        case MCUBOOT_STATUS_BOOT_FAILED:
            LOG_ERR("### MCUboot status: %s", "BOOT_FAILED");
            break;
        default:
            LOG_ERR("### MCUboot status: %d", status);
            break;
    }
}

/**
 * Redefine invalidate_public_key function
 */
void
__wrap_invalidate_public_key(uint32_t key_idx) // NOSONAR
{
    /* This function can be called during firmware signature validation,
       but invalidating of a public key can be done only from B0 (NSIB) bootloader.
       So, we do nothing here.
       When device will be restarted and enters B0 (NSIB) bootloader,
       invalidation will take place.
    */
}
