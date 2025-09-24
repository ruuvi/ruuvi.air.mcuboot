/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/fs/fs.h>
#include <zephyr/app_version.h>
#include <sysflash/pm_sysflash.h>
#include <bl_validation.h>
#include <bootutil/mcuboot_status.h>
#include <bootutil/bootutil.h>
#include <bootutil/boot_status.h>
#include <bootutil/boot_record.h>
#include <bootutil/fault_injection_hardening.h>
#include <fw_info_bare.h>
#include <fw_info.h>
#include "mcuboot_img_op.h"
#include "btldr_fs.h"
#include "file_img_validate.h"
#include "file_tlv_priv.h"
#include "mcuboot_segger_rtt.h"
#include "mcuboot_version.h"
#include "app_version.h"
#include "ncs_version.h"
#include "version.h"
#include "app_commit.h"
#include "ncs_commit.h"
#include "zephyr_commit.h"

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

#define RUUVI_FW_MCUBOOT0_FILE_NAME "signed_by_mcuboot_and_b0_mcuboot.bin"
#define RUUVI_FW_MCUBOOT1_FILE_NAME "signed_by_mcuboot_and_b0_s1_image.bin"
#define RUUVI_FW_LOADER_FILE_NAME   "ruuvi_air_fw_loader.signed.bin"
#define RUUVI_FW_APP_FILE_NAME      "ruuvi_air_fw.signed.bin"

#define MCUBOOT_HOOK_TMPBUF_SZ 256

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

#if defined(MCUBOOT_DOWNGRADE_PREVENTION)
/* s0/s1 package version of the current MCUboot image,
 * the values gets from SB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION
 */
static const struct image_version mcuboot_s0_s1_image_version = {
    .iv_major     = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MAJOR,
    .iv_minor     = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MINOR,
    .iv_revision  = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_REVISION,
    .iv_build_num = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_BUILD_NUMBER,
};
#endif

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
static int
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

static const char*
get_image_slot_name(const int fa_id)
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

static bool
load_image_header(const int fa_id, struct image_header* const p_img_hdr)
{
    bool is_success = false;

    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        return false;
    }

    rc = boot_image_load_header(p_fa, p_img_hdr);
    if (0 != rc)
    {
        goto func_exit;
    }

    is_success = true;

func_exit:
    flash_area_close(p_fa);
    return is_success;
}

static uint32_t
app_max_size(const int fa_id1, const int fa_id2)
{
    int                      rc   = 0;
    const struct flash_area* p_fa = NULL;

    rc = flash_area_open(fa_id1, &p_fa);
    assert(rc == 0);
    const uint32_t primary_sz = flash_area_get_size(p_fa);
    flash_area_close(p_fa);

    rc = flash_area_open(fa_id2, &p_fa);
    assert(rc == 0);
    const uint32_t secondary_sz = flash_area_get_size(p_fa);
    flash_area_close(p_fa);

    return (secondary_sz < primary_sz) ? secondary_sz : primary_sz;
}

static bool
get_flash_area_address_and_size(const int fa_id, uint32_t* const p_fa_addr, uint32_t* const p_fa_size)
{
    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(fa_id, &p_fa);
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

static bool
boot_add_shared_data(const int active_slot, const int active_fa_id, const struct flash_area* p_fa)
{
#if defined(MCUBOOT_MEASURED_BOOT) || defined(MCUBOOT_DATA_SHARING)
    int rc = 0;

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
            .max_size = app_max_size(FLASH_AREA_IMAGE_PRIMARY(0), FLASH_AREA_IMAGE_SECONDARY(0)),
        },
        [CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] = {
            .calculated = true,
            .max_size = app_max_size(
                // Use the primary slot only in both cases.
                // When primary bootloader is active, the 's1' slot is used as primary,
                // and when secondary bootloader is active, the 's0' slot is used as primary,
                // and in both cases 'fw_loader' is in the secondary slot.
                FLASH_AREA_IMAGE_PRIMARY(CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER),
                FLASH_AREA_IMAGE_PRIMARY(CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER)),
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
save_shared_data_for_active_slot(uint8_t mcuboot_active_slot, const int mcuboot_active_fa_id)
{
    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(mcuboot_active_fa_id, &p_fa);
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

static bool
img_invalidate(const int fa_id)
{
    LOG_INF("Invalidate image in flash area %d (%s)", fa_id, get_image_slot_name(fa_id));
    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(fa_id, &p_fa);
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

static __NO_RETURN void
reboot_cold(void)
{
    LOG_INF("Rebooting (cold)...");
    k_msleep(500);
    sys_reboot(SYS_REBOOT_COLD);
}

static struct fs_file_t
open_file_and_load_image_header(
    const char* const          p_file_name,
    struct image_header* const p_img_hdr,
    uint32_t* const            p_img_size)
{
    bool is_success = false;

    struct fs_file_t file = btldr_fs_open_file(p_file_name);
    if (NULL == file.filep)
    {
        return file;
    }

    const ssize_t len = fs_read(&file, p_img_hdr, sizeof(*p_img_hdr));
    if (len < 0)
    {
        LOG_ERR("Failed reading image header from file %s, rc=%zd", p_file_name, len);
        goto func_exit;
    }
    if (len != sizeof(*p_img_hdr))
    {
        LOG_ERR(
            "Failed reading image header from file %s, len=%zd, expected len=%zu",
            p_file_name,
            len,
            sizeof(*p_img_hdr));
        goto func_exit;
    }

    if (p_img_hdr->ih_magic != IMAGE_MAGIC)
    {
        LOG_ERR("Bad image magic in file %s: 0x%08" PRIx32, p_file_name, p_img_hdr->ih_magic);
        goto func_exit;
    }

    if (p_img_hdr->ih_flags & IMAGE_F_NON_BOOTABLE)
    {
        LOG_ERR("Image not bootable in file %s", p_file_name);
        goto func_exit;
    }

    if (!boot_u32_safe_add(p_img_size, p_img_hdr->ih_img_size, p_img_hdr->ih_hdr_size))
    {
        LOG_ERR(
            "Image size overflow in file %s, ih_img_size=%" PRIu32 ", ih_hdr_size=%" PRIu32,
            p_file_name,
            p_img_hdr->ih_img_size,
            p_img_hdr->ih_hdr_size);
        goto func_exit;
    }

    const off_t file_size = btldr_fs_get_file_size(&file);
    if (*p_img_size > file_size)
    {
        LOG_ERR(
            "Image size in file %s is bigger than the file, file_size=%" PRIu32 ", image size=%" PRIu32,
            p_file_name,
            (uint32_t)file_size,
            *p_img_size);
        goto func_exit;
    }

    is_success = true;

func_exit:
    if (!is_success)
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
    struct image_header* const p_img_hdr)
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
    FIH_CALL(file_img_validate, validity_res, &img_hdr, &file, dst_fa_size, tmp_buf, sizeof(tmp_buf), NULL, 0, NULL);
    if (FIH_NOT_EQ(validity_res, FIH_SUCCESS))
    {
        LOG_ERR("Validation failed for file: %s", p_file_name);
        return false;
    }
    int rc = fs_seek(&file, img_hdr.ih_hdr_size + sizeof(uint32_t), FS_SEEK_SET);
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

    fs_close(&file);

    return true;
}

static bool
fw_info_check_in_file(struct fs_file_t* const p_file, off_t offset, struct fw_info* const p_fw_info)
{
    int rc = fs_seek(p_file, offset, FS_SEEK_SET);
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

    static const uint32_t fw_info_magic[] = { FIRMWARE_INFO_MAGIC };

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
    for (uint32_t i = 0; i < FW_INFO_OFFSET_COUNT; i++)
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

static void
print_image_info(const int fa_id)
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
    LOG_INF(
        "### Flash area %d (%s): Image version: v%u.%u.%u+%u, FwInfoVer: %u",
        fa_id,
        get_image_slot_name(fa_id),
        img_hdr.ih_ver.iv_major,
        img_hdr.ih_ver.iv_minor,
        img_hdr.ih_ver.iv_revision,
        img_hdr.ih_ver.iv_build_num,
        p_fw_info->version);
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
check_file_and_update(const char* const p_file_name, const int dst_fa_id, const bool flag_validate_b0_signature)
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

    struct image_header file_img_hdr = { 0 };
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
        if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, &file_img_hdr))
        {
            LOG_WRN("MCUboot signature for file %s is not valid, but B0 signature is valid", p_file_name);
        }
    }
    else
    {
        if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, &file_img_hdr))
        {
            LOG_ERR("File %s contains invalid image", p_file_name);
            btldr_fs_unlink_file(p_file_name);
            return false;
        }
        LOG_INF("File %s validated successfully", p_file_name);
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
    struct image_header dst_img_hdr = { 0 };
    if (!load_image_header(dst_fa_id, &dst_img_hdr))
    {
        LOG_ERR("Failed to load image header for slot fa_id=%d", dst_fa_id);
        btldr_fs_unlink_file(p_file_name);
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
        file_img_hdr.ih_ver.iv_major,
        file_img_hdr.ih_ver.iv_minor,
        file_img_hdr.ih_ver.iv_revision,
        file_img_hdr.ih_ver.iv_build_num);
    int rc = boot_version_cmp(&file_img_hdr.ih_ver, &dst_img_hdr.ih_ver);
    if ((rc >= 0) && (dst_fa_id == PM_ID(s0) || dst_fa_id == PM_ID(s1)))
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
        int version_check = boot_version_cmp(&file_img_hdr.ih_ver, &mcuboot_s0_s1_image_version);
        /* Only update rc if the currently running version is newer */
        if (version_check < rc)
        {
            rc = version_check;
        }
    }
    if (rc < 0)
    {
        LOG_ERR("Downgrade prevention: New image version is older than the current image version");
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
check_update_for_mcuboot(const char* const p_file_name, const int dst_fa_id)
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

    if (!validate_file(p_file_name, dst_fa_addr, dst_fa_size, NULL))
    {
        LOG_WRN("MCUboot signature for file %s is not valid, but B0 signature is valid", p_file_name);
    }

    return true;
}

static bool
check_updates_on_fs(const uint8_t mcuboot_active_slot)
{
    bool flag_updates_found = false;

    if (0 == mcuboot_active_slot)
    {
        flag_updates_found |= check_file_and_update(RUUVI_FW_MCUBOOT1_FILE_NAME, PM_ID(s1), true);
        if (check_update_for_mcuboot(RUUVI_FW_MCUBOOT0_FILE_NAME, PM_ID(s0)))
        {
            LOG_INF("Found file %s - need to reboot to update it from secondary MCUboot", RUUVI_FW_MCUBOOT0_FILE_NAME);
            (void)img_invalidate(PM_ID(s0));
            return true;
        }
    }
    else
    {
        flag_updates_found |= check_file_and_update(RUUVI_FW_MCUBOOT0_FILE_NAME, PM_ID(s0), true);

        if (check_update_for_mcuboot(RUUVI_FW_MCUBOOT1_FILE_NAME, PM_ID(s1)))
        {
            LOG_INF("Found file %s - need to reboot to update it from primary MCUboot", RUUVI_FW_MCUBOOT1_FILE_NAME);
            return true;
        }
    }
    flag_updates_found |= check_file_and_update(RUUVI_FW_LOADER_FILE_NAME, PM_ID(mcuboot_secondary), false);
    flag_updates_found |= check_file_and_update(RUUVI_FW_APP_FILE_NAME, PM_ID(mcuboot_primary), false);
    return flag_updates_found;
}

static void
on_startup(void)
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

    mcuboot_segger_rtt_check_data_location_and_size();

#if 0
    LOG_INF("Seeep 10 seconds...");
    k_msleep(10000);
    LOG_INF("Wake up");
#endif

#ifndef CONFIG_NCS_IS_VARIANT_IMAGE
    const uint8_t mcuboot_active_slot = 0;
#else
    const uint8_t mcuboot_active_slot = 1;
#endif

    const int mcuboot_active_fa_id = (0 == mcuboot_active_slot) ? PM_ID(s0) : PM_ID(s1);
    LOG_INF(
        "### MCUboot: Active slot: %s (%s), id=%d",
        (0 == mcuboot_active_slot) ? "primary" : "secondary",
        (0 == mcuboot_active_slot) ? "s0" : "s1",
        mcuboot_active_fa_id);

    LOG_INF("### MCUboot: primary area id=%d", FLASH_AREA_IMAGE_PRIMARY(0));
    LOG_INF("### MCUboot: secondary area id=%d", FLASH_AREA_IMAGE_SECONDARY(0));

    print_image_info(PM_ID(s0));
    print_image_info(PM_ID(s1));
    print_image_info(PM_ID(mcuboot_primary));
    print_image_info(PM_ID(mcuboot_secondary));

    if (btldr_fs_mount())
    {
        if (check_updates_on_fs(mcuboot_active_slot))
        {
            reboot_cold();
        }
        btldr_fs_unmount();
    }

    save_shared_data_for_active_slot(mcuboot_active_slot, mcuboot_active_fa_id);

#if defined(CONFIG_USE_SEGGER_RTT)
    k_msleep(500);
#endif
}

static void
on_bootable_image_found(void)
{
    LOG_INF("### MCUboot status: %s", "BOOTABLE_IMAGE_FOUND");
#if USE_PARTITION_MANAGER && CONFIG_FPROTECT
    LOG_INF("Protecting MCUBoot flash area, address: 0x%x, size: 0x%x", PROTECT_ADDR, PROTECT_SIZE);
#endif
    k_msleep(200);
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
    LOG_WRN("wrap_invalidate_public_key - do nothing");
}
