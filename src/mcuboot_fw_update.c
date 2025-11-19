/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mcuboot_fw_update.h"
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <sysflash/pm_sysflash.h>
#include <fw_info_bare.h>
#include <fw_info.h>
#include <bootutil/bootutil.h>
#include <bootutil/fault_injection_hardening.h>
#include <bl_validation.h>
#include "file_img_validate.h"
#include "btldr_fs.h"
#include "ruuvi_fw_update.h"
#include "mcuboot_fa_utils.h"
#include "mcuboot_img_op.h"
#include "file_tlv_priv.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#define MCUBOOT_HOOK_TMPBUF_SZ 256

#define SHARED_NODE DT_NODELABEL(shared_sram)

_Static_assert(PM_S0_SIZE == PM_S1_SIZE, "PM_S0_SIZE must be equal to PM_S1_SIZE");
_Static_assert(
    PM_S0_SIZE == DT_REG_SIZE(DT_NODELABEL(shared_sram)),
    "PM_S0_SIZE must be equal to size of linker section 'shared_sram'");
_Static_assert(
    PM_S1_SIZE == DT_REG_SIZE(DT_NODELABEL(shared_sram)),
    "PM_S1_SIZE must be equal to size of linker section 'shared_sram'");

static __aligned(4) __attribute__((used)) uint8_t g_shared_img_buf[MAX(PM_S0_SIZE, PM_S1_SIZE)] Z_GENERIC_SECTION(
    LINKER_DT_NODE_REGION_NAME(SHARED_NODE));

static __NO_RETURN void
reboot_cold(void)
{
    LOG_INF("Rebooting (cold)...");
    k_msleep(500); // NOSONAR
    sys_reboot(SYS_REBOOT_COLD);
}

static bool
img_invalidate(const fa_id_t fa_id)
{
    LOG_INF("Invalidate image in flash area %d (%s)", fa_id, get_image_slot_name(fa_id));
    const struct flash_area* p_fa = NULL;
    int32_t                  rc   = flash_area_open(fa_id, &p_fa);
    if (rc != 0)
    {
        LOG_ERR("Failed to open flash area %d (%s), rc=%d", fa_id, get_image_slot_name(fa_id), rc);
        return false;
    }

    const struct fw_info* p_fw_info = fw_info_find(p_fa->fa_off);
    if (NULL == p_fw_info)
    {
        LOG_ERR("Failed to find fw_info for flash area %d (%s)", fa_id, get_image_slot_name(fa_id));
        flash_area_close(p_fa);
        return false;
    }

    fw_info_invalidate(p_fw_info);

    flash_area_close(p_fa);

    return true;
}

static bool
validate_b0_signature(const char* const p_file_name, const uint32_t dst_fa_addr, const uint32_t dst_fa_size)
{
    if (dst_fa_size != sizeof(g_shared_img_buf))
    {
        LOG_ERR("%s: Invalid flash area size %u, expected %zu", __func__, dst_fa_size, sizeof(g_shared_img_buf));
        return false;
    }

    struct fs_file_t file = btldr_fs_open_file(p_file_name);
    if (NULL == file.filep)
    {
        return false;
    }

    const off_t file_size = btldr_fs_get_file_size(&file);
    if (file_size > sizeof(g_shared_img_buf))
    {
        LOG_ERR(
            "%s: File size %" PRIu32 " is too big for buffer, max size=%zu",
            __func__,
            (uint32_t)file_size,
            sizeof(g_shared_img_buf));
        fs_close(&file);
        return false;
    }
    ssize_t rc = fs_read(&file, g_shared_img_buf, file_size);
    if (rc < 0)
    {
        LOG_ERR("%s: Failed to read file, rc=%zd", __func__, rc);
        fs_close(&file);
        return false;
    }
    if (rc != file_size)
    {
        LOG_ERR(
            "%s: Failed to read file, read %u bytes, expected %u bytes",
            __func__,
            (unsigned)rc,
            (unsigned)file_size);
        fs_close(&file);
        return false;
    }

    const struct fw_info* const p_fw_info = fw_info_find((uint32_t)g_shared_img_buf);
    if (NULL == p_fw_info)
    {
        LOG_ERR("%s: Failed to find fw_info in file %s", __func__, p_file_name);
        fs_close(&file);
        return false;
    }
    const uint32_t addr_offset = p_fw_info->address - dst_fa_addr;
    if (addr_offset >= sizeof(g_shared_img_buf))
    {
        LOG_ERR("%s: Invalid address offset 0x%08x", __func__, addr_offset);
        fs_close(&file);
        return false;
    }
    if (!bl_validate_firmware(p_fw_info->address, (uint32_t)&g_shared_img_buf[addr_offset]))
    {
        LOG_ERR("%s: Failed to validate firmware in file %s", __func__, p_file_name);
        fs_close(&file);
        return false;
    }
    fs_close(&file);
    return true;
}

static bool
load_image_header_from_file(
    struct fs_file_t* const    p_file,
    const char* const          p_file_name,
    struct image_header* const p_img_hdr,
    uint32_t* const            p_img_size)
{
    const ssize_t len = fs_read(p_file, p_img_hdr, sizeof(*p_img_hdr));
    if (len < 0)
    {
        LOG_ERR("Failed reading image header from file %s, rc=%zd", p_file_name, len);
        return false;
    }
    if (len != sizeof(*p_img_hdr))
    {
        LOG_ERR(
            "Failed reading image header from file %s, len=%zd, expected len=%zu",
            p_file_name,
            len,
            sizeof(*p_img_hdr));
        return false;
    }

    if (p_img_hdr->ih_magic != IMAGE_MAGIC)
    {
        LOG_ERR("Bad image magic in file %s: 0x%08" PRIx32, p_file_name, p_img_hdr->ih_magic);
        return false;
    }

    if (0 != (p_img_hdr->ih_flags & IMAGE_F_NON_BOOTABLE))
    {
        LOG_ERR("Image not bootable in file %s", p_file_name);
        return false;
    }

    if (!boot_u32_safe_add(p_img_size, p_img_hdr->ih_img_size, p_img_hdr->ih_hdr_size))
    {
        LOG_ERR(
            "Image size overflow in file %s, ih_img_size=%" PRIu32 ", ih_hdr_size=%" PRIu32,
            p_file_name,
            p_img_hdr->ih_img_size,
            p_img_hdr->ih_hdr_size);
        return false;
    }

    const off_t file_size = btldr_fs_get_file_size(p_file);
    if (*p_img_size > file_size)
    {
        LOG_ERR(
            "Image size in file %s is bigger than the file, file_size=%" PRIu32 ", image size=%" PRIu32,
            p_file_name,
            (uint32_t)file_size,
            *p_img_size);
        return false;
    }
    return true;
}

static struct fs_file_t
open_file_and_load_image_header(
    const char* const          p_file_name,
    struct image_header* const p_img_hdr,
    uint32_t* const            p_img_size)
{
    struct fs_file_t file = btldr_fs_open_file(p_file_name);
    if (NULL == file.filep)
    {
        return file;
    }
    if (!load_image_header_from_file(&file, p_file_name, p_img_hdr, p_img_size))
    {
        btldr_fs_close_file(&file);
        file.filep = NULL;
    }
    return file;
}

static bool
validate_file(
    const char* const          p_file_name,
    const uint32_t             dst_fa_addr,
    const uint32_t             dst_fa_size,
    struct image_header* const p_img_hdr,
    fw_image_hw_rev_t* const   p_hw_rev)
{
    static uint8_t tmp_buf[MCUBOOT_HOOK_TMPBUF_SZ];

    if (!btldr_fs_is_file_exist(p_file_name))
    {
        return false;
    }

    LOG_INF("Validate image in file %s", p_file_name);

    struct image_header img_hdr  = { 0 };
    uint32_t            img_size = 0;
    struct fs_file_t    file     = open_file_and_load_image_header(p_file_name, &img_hdr, &img_size);
    if (NULL == file.filep)
    {
        LOG_ERR("Failed to load image header from file %s", p_file_name);
        return false;
    }
    if (img_size >= dst_fa_size)
    {
        LOG_ERR("Image size %" PRIu32 " is too big for flash area, max size=%" PRIu32, img_size, dst_fa_size);
        return false;
    }
    if (NULL != p_img_hdr)
    {
        *p_img_hdr = img_hdr;
    }

    FIH_DECLARE(validity_res, FIH_FAILURE);
    FIH_CALL(file_img_validate, validity_res, &img_hdr, &file, dst_fa_size, tmp_buf, sizeof(tmp_buf), NULL, 0);
    if (FIH_NOT_EQ(validity_res, FIH_SUCCESS))
    {
        LOG_ERR("Validation failed for file: %s", p_file_name);
        return false;
    }
    zephyr_api_ret_t rc = fs_seek(&file, img_hdr.ih_hdr_size + sizeof(uint32_t), FS_SEEK_SET);
    if (0 != rc)
    {
        LOG_ERR("Failed to seek to the beginning of the image data in file %s, rc=%d", p_file_name, rc);
        return false;
    }
    uint32_t reset_addr = 0;
    rc                  = fs_read(&file, &reset_addr, sizeof(reset_addr));
    if (rc < 0)
    {
        LOG_ERR("Failed to read reset address from file %s, rc=%d", p_file_name, rc);
        return false;
    }
    if (rc != sizeof(reset_addr))
    {
        LOG_ERR(
            "Failed to read reset address from file %s, read %u bytes, expected %zu bytes",
            p_file_name,
            (unsigned)rc,
            sizeof(reset_addr));
        return false;
    }
    if (!((reset_addr >= dst_fa_addr) && (reset_addr < (dst_fa_addr + dst_fa_size))))
    {
        LOG_ERR(
            "Reset address 0x%08" PRIx32 " is out of flash area 0x%08" PRIx32 " .. 0x%08" PRIx32,
            reset_addr,
            dst_fa_addr,
            dst_fa_addr + dst_fa_size);
        return false;
    }

    fw_image_hw_rev_t hw_rev = { 0 };
    if (!fw_img_hw_rev_find_in_file(&file, &hw_rev))
    {
        LOG_WRN("Image in file %s: No Ruuvi HW revision TLVs found", p_file_name);
    }
    else
    {
        LOG_DBG(
            "Image in file %s: Found Ruuvi HW revision TLVs: ID=%" PRIu32 ", name='%s'",
            p_file_name,
            hw_rev.hw_rev_num,
            hw_rev.hw_rev_name);
    }
    if (NULL != p_hw_rev)
    {
        *p_hw_rev = hw_rev;
    }

    fs_close(&file);

    return true;
}

static bool
check_file(
    const char* const          p_file_name,
    const uint32_t             dst_fa_addr,
    const uint32_t             dst_fa_size,
    const bool                 flag_validate_b0_signature,
    struct image_header* const p_file_img_hdr,
    fw_image_hw_rev_t* const   p_hw_rev)
{
    if (!btldr_fs_is_file_exist(p_file_name))
    {
        return false;
    }

    if (flag_validate_b0_signature)
    {
        LOG_INF("Validate B0 signature for file: %s", p_file_name);
        if (!validate_b0_signature(p_file_name, dst_fa_addr, dst_fa_size))
        {
            LOG_ERR("Failed to validate B0 signature for file %s", p_file_name);
            btldr_fs_unlink_file(p_file_name);
            return false;
        }
        LOG_INF("B0 signature in file %s validated successfully", p_file_name);
        if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, p_file_img_hdr, p_hw_rev))
        {
            LOG_WRN("MCUboot signature for file %s is not valid, but B0 signature is valid", p_file_name);
        }
    }
    else
    {
        if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, p_file_img_hdr, p_hw_rev))
        {
            LOG_ERR("File %s contains invalid image", p_file_name);
            btldr_fs_unlink_file(p_file_name);
            return false;
        }
        LOG_INF("File %s validated successfully", p_file_name);
    }
    return true;
}

static bool
fw_info_check_in_file(struct fs_file_t* const p_file, off_t offset, struct fw_info* const p_fw_info)
{
    zephyr_api_ret_t rc = fs_seek(p_file, offset, FS_SEEK_SET);
    if (0 != rc)
    {
        LOG_ERR("Failed to seek to offset %ld in file, rc=%d", offset, rc);
        return false;
    }
    const ssize_t len = fs_read(p_file, p_fw_info, sizeof(*p_fw_info));
    if (len < 0)
    {
        LOG_ERR("Failed reading fw_info, rc=%zd", len);
        return false;
    }
    if (len != sizeof(*p_fw_info))
    {
        LOG_ERR("Failed reading fw_info, len=%zd, expected len=%zu", len, sizeof(*p_fw_info));
        return false;
    }

    static const uint32_t fw_info_magic[/* NOSONAR */] = { FIRMWARE_INFO_MAGIC };

    if (0 == memcmp(p_fw_info->magic, fw_info_magic, CONFIG_FW_INFO_MAGIC_LEN))
    {
        return true;
    }
    return false;
}

static bool
fw_info_find_in_file(const char* const p_file_name, struct fw_info* const p_fw_info)
{
    struct fs_file_t file = btldr_fs_open_file(p_file_name);
    if (NULL == file.filep)
    {
        return false;
    }

    bool flag_fw_info_found = false;
    for (uint32_t i = 0; i < FW_INFO_OFFSET_COUNT; ++i)
    {
        if (fw_info_check_in_file(&file, fw_info_allowed_offsets[i], p_fw_info))
        {
            flag_fw_info_found = true;
            break;
        }
    }
    fs_close(&file);
    return flag_fw_info_found;
}

#if defined(MCUBOOT_DOWNGRADE_PREVENTION)
/**
 * Compare image version numbers
 *
 * By default, the comparison does not take build number into account.
 * Enable MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER to take the build number into account.
 *
 * @param ver1           Pointer to the first image version to compare.
 * @param ver2           Pointer to the second image version to compare.
 *
 * @retval -1           If ver1 is less than ver2.
 * @retval 0            If the image version numbers are equal.
 * @retval 1            If ver1 is greater than ver2.
 */
static int32_t
boot_version_cmp(const struct image_version* ver1, const struct image_version* ver2)
{
    if (ver1->iv_major > ver2->iv_major)
    {
        return 1;
    }
    if (ver1->iv_major < ver2->iv_major)
    {
        return -1;
    }
    /* The major version numbers are equal, continue comparison. */
    if (ver1->iv_minor > ver2->iv_minor)
    {
        return 1;
    }
    if (ver1->iv_minor < ver2->iv_minor)
    {
        return -1;
    }
    /* The minor version numbers are equal, continue comparison. */
    if (ver1->iv_revision > ver2->iv_revision)
    {
        return 1;
    }
    if (ver1->iv_revision < ver2->iv_revision)
    {
        return -1;
    }

#if defined(MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER)
    /* The revisions are equal, continue comparison. */
    if (ver1->iv_build_num > ver2->iv_build_num)
    {
        return 1;
    }
    if (ver1->iv_build_num < ver2->iv_build_num)
    {
        return -1;
    }
#endif

    return 0;
}
#endif

#if defined(MCUBOOT_DOWNGRADE_PREVENTION)
static bool
check_downgrade_prevention(const fa_id_t dst_fa_id, const struct image_header* const p_file_img_hdr)
{
    struct image_header dst_img_hdr = { 0 };
    if (!load_image_header(dst_fa_id, &dst_img_hdr))
    {
        LOG_ERR("Failed to load image header for slot fa_id=%d", dst_fa_id);
        return false;
    }
    LOG_INF(
        "Current image version: %u.%u.%u.%u",
        dst_img_hdr.ih_ver.iv_major,
        dst_img_hdr.ih_ver.iv_minor,
        dst_img_hdr.ih_ver.iv_revision,
        dst_img_hdr.ih_ver.iv_build_num);
    LOG_INF(
        "New image version: %u.%u.%u.%u",
        p_file_img_hdr->ih_ver.iv_major,
        p_file_img_hdr->ih_ver.iv_minor,
        p_file_img_hdr->ih_ver.iv_revision,
        p_file_img_hdr->ih_ver.iv_build_num);
    int32_t rc = boot_version_cmp(&p_file_img_hdr->ih_ver, &p_file_img_hdr->ih_ver);
    if ((rc >= 0) && ((PM_ID(s0) == dst_fa_id) || (PM_ID(s1) == dst_fa_id)))
    {
        /* Also check the new version of MCUboot against that of the current s0/s1 MCUboot
         * trailer version to prevent downgrades
         */
        LOG_INF(
            "MCUboot version: %u.%u.%u.%u",
            mcuboot_s0_s1_image_version.iv_major,
            mcuboot_s0_s1_image_version.iv_minor,
            mcuboot_s0_s1_image_version.iv_revision,
            mcuboot_s0_s1_image_version.iv_build_num);
        const int32_t version_check = boot_version_cmp(&p_file_img_hdr->ih_ver, &mcuboot_s0_s1_image_version);
        /* Only update rc if the currently running version is newer */
        if (version_check < rc)
        {
            rc = version_check;
        }
    }
    if (rc < 0)
    {
        LOG_ERR("Downgrade prevention: New image version is older than the current image version");
        return false;
    }
    return true;
}
#endif // MCUBOOT_DOWNGRADE_PREVENTION

static bool
check_file_and_update(
    const char* const              p_file_name,
    const fa_id_t                  dst_fa_id,
    const fw_image_hw_rev_t* const p_hw_rev,
    const bool                     flag_validate_b0_signature)
{
    uint32_t dst_fa_addr = 0;
    uint32_t dst_fa_size = 0;
    if (!get_flash_area_address_and_size(dst_fa_id, &dst_fa_addr, &dst_fa_size))
    {
        LOG_ERR("Failed to get flash area address and size for id=%d", dst_fa_id);
        return false;
    }

    struct image_header file_img_hdr = { 0 };
    fw_image_hw_rev_t   hw_rev       = { 0 };
    if (!check_file(p_file_name, dst_fa_addr, dst_fa_size, flag_validate_b0_signature, &file_img_hdr, &hw_rev))
    {
        return false;
    }

    struct fw_info file_fw_info = { 0 };
    if (!fw_info_find_in_file(p_file_name, &file_fw_info))
    {
        LOG_ERR("Failed to find fw_info in file %s", p_file_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
    const struct fw_info* const p_dst_fw_info = fw_info_find(dst_fa_addr);
    if (NULL == p_dst_fw_info)
    {
        LOG_ERR("Failed to find fw_info for flash area %d (%s)", dst_fa_id, get_image_slot_name(dst_fa_id));
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
    LOG_INF(
        "Image in file %s: Image version: v%u.%u.%u+%u, FwInfoVer: %u, HwRev: ID=%" PRIu32 ", name='%s'",
        p_file_name,
        file_img_hdr.ih_ver.iv_major,
        file_img_hdr.ih_ver.iv_minor,
        file_img_hdr.ih_ver.iv_revision,
        file_img_hdr.ih_ver.iv_build_num,
        file_fw_info.version,
        hw_rev.hw_rev_num,
        hw_rev.hw_rev_name);

    if (('\0' != p_hw_rev->hw_rev_name[0]) && (0 != strcmp(p_hw_rev->hw_rev_name, hw_rev.hw_rev_name)))
    {
        LOG_ERR("HW revision name mismatch: expected '%s', got '%s'", p_hw_rev->hw_rev_name, hw_rev.hw_rev_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }

    LOG_INF("Current image FwInfoVersion: %u", p_dst_fw_info->version);
    LOG_INF("New image FwInfoVersion: %u", file_fw_info.version);
    if (p_dst_fw_info->version > file_fw_info.version)
    {
        LOG_ERR(
            "Downgrade prevention: New image version(%u) is older than the current image version(%u)",
            file_fw_info.version,
            p_dst_fw_info->version);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }

#if defined(MCUBOOT_DOWNGRADE_PREVENTION)
    if (!check_downgrade_prevention(dst_fa_id, &file_img_hdr))
    {
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
#endif

    struct fs_file_t file = btldr_fs_open_file(p_file_name);
    if (NULL == file.filep)
    {
        LOG_ERR("Failed to open file %s", p_file_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
    LOG_INF(
        "Copy firmware from file %s to flash partition %d (%s)",
        p_file_name,
        dst_fa_id,
        get_image_slot_name(dst_fa_id));
    if (mcuboot_img_op_copy(dst_fa_id, &file))
    {
        LOG_INF("%s copied successfully", p_file_name);
    }
    btldr_fs_close_file(&file);
    btldr_fs_unlink_file(p_file_name);
    return true;
}

static bool
check_update_for_mcuboot(
    const char* const              p_file_name,
    const fa_id_t                  dst_fa_id,
    const fw_image_hw_rev_t* const p_hw_rev)
{
    if (!btldr_fs_is_file_exist(p_file_name))
    {
        return false;
    }
    uint32_t dst_fa_addr = 0;
    uint32_t dst_fa_size = 0;
    if (!get_flash_area_address_and_size(dst_fa_id, &dst_fa_addr, &dst_fa_size))
    {
        LOG_ERR("Failed to get flash area address and size for id=%d", dst_fa_id);
        return false;
    }

    LOG_INF(
        "Validate B0 signature for file: %s, dst_addr=0x%" PRIx32 ", size=0x%" PRIx32,
        p_file_name,
        dst_fa_addr,
        dst_fa_size);
    if (!validate_b0_signature(p_file_name, dst_fa_addr, dst_fa_size))
    {
        LOG_ERR("Failed to validate B0 signature for file %s", p_file_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
    LOG_INF("B0 signature for file %s validated successfully", p_file_name);

    struct image_header file_img_hdr = { 0 };
    fw_image_hw_rev_t   hw_rev       = { 0 };
    if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, &file_img_hdr, &hw_rev))
    {
        LOG_WRN("MCUboot signature for file %s is not valid, but B0 signature is valid", p_file_name);
    }

    struct fw_info file_fw_info = { 0 };
    if (!fw_info_find_in_file(p_file_name, &file_fw_info))
    {
        LOG_ERR("Failed to find fw_info in file %s", p_file_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }

    LOG_INF(
        "Image in file %s: Image version: v%u.%u.%u+%u, FwInfoVer: %u, HwRev: ID=%" PRIu32 ", name='%s'",
        p_file_name,
        file_img_hdr.ih_ver.iv_major,
        file_img_hdr.ih_ver.iv_minor,
        file_img_hdr.ih_ver.iv_revision,
        file_img_hdr.ih_ver.iv_build_num,
        file_fw_info.version,
        hw_rev.hw_rev_num,
        hw_rev.hw_rev_name);

    if (('\0' != p_hw_rev->hw_rev_name[0]) && (0 != strcmp(p_hw_rev->hw_rev_name, hw_rev.hw_rev_name)))
    {
        LOG_ERR("HW revision name mismatch: expected '%s', got '%s'", p_hw_rev->hw_rev_name, hw_rev.hw_rev_name);
        btldr_fs_unlink_file(p_file_name);
        return false;
    }
    return true;
}

static bool
check_updates_on_fs(const uint8_t mcuboot_active_slot, const fw_image_hw_rev_t* const p_hw_rev)
{
    bool flag_updates_found = false;

    if (0 == mcuboot_active_slot)
    {
        const bool flag_validate_b0_signature = true;
        if (check_file_and_update(
                RUUVI_FW_MCUBOOT1_FILE_NAME,
                (fa_id_t)PM_ID(s1),
                p_hw_rev,
                flag_validate_b0_signature))
        {
            flag_updates_found = true;
        }
        if (check_update_for_mcuboot(RUUVI_FW_MCUBOOT0_FILE_NAME, (fa_id_t)PM_ID(s0), p_hw_rev))
        {
            LOG_INF("Found file %s - need to reboot to update it from secondary MCUboot", RUUVI_FW_MCUBOOT0_FILE_NAME);
            (void)img_invalidate((fa_id_t)PM_ID(s0));
            return true;
        }
    }
    else
    {
        const bool flag_validate_b0_signature = true;
        if (check_file_and_update(
                RUUVI_FW_MCUBOOT0_FILE_NAME,
                (fa_id_t)PM_ID(s0),
                p_hw_rev,
                flag_validate_b0_signature))
        {
            flag_updates_found = true;
        }

        if (check_update_for_mcuboot(RUUVI_FW_MCUBOOT1_FILE_NAME, (fa_id_t)PM_ID(s1), p_hw_rev))
        {
            LOG_INF("Found file %s - need to reboot to update it from primary MCUboot", RUUVI_FW_MCUBOOT1_FILE_NAME);
            return true;
        }
    }
    const bool flag_validate_b0_signature = false;
    if (check_file_and_update(
            RUUVI_FW_LOADER_FILE_NAME,
            (fa_id_t)PM_ID(mcuboot_secondary),
            p_hw_rev,
            flag_validate_b0_signature))
    {
        flag_updates_found = true;
    }
    if (check_file_and_update(
            RUUVI_FW_APP_FILE_NAME,
            (fa_id_t)PM_ID(mcuboot_primary),
            p_hw_rev,
            flag_validate_b0_signature))
    {
        flag_updates_found = true;
    }
    return flag_updates_found;
}

void
mcuboot_fw_update(const slot_id_t mcuboot_active_slot, const fw_image_hw_rev_t* const p_hw_rev)
{
    if (btldr_fs_mount())
    {
        if (check_updates_on_fs(mcuboot_active_slot, p_hw_rev))
        {
            reboot_cold();
        }
        btldr_fs_unmount();
    }
}
