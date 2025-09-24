#include "mcuboot_img_op.h"
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <cmsis_gcc.h>

LOG_MODULE_DECLARE(B0, LOG_LEVEL_INF);

#define TMP_BUF_SIZE 256

typedef bool (*cb_img_process_t)(
    const struct flash_area* p_fa_dst,
    const off_t              offset,
    const uint8_t*           p_src_img_data_buf,
    const size_t             buf_len);

static bool
img_process(
    const int               fa_id_dst,
    struct fs_file_t* const p_file_src,
    const bool              flag_erase_dst,
    cb_img_process_t        cb_img_process)
{
    static uint8_t tmp_buf1[TMP_BUF_SIZE];

    const struct flash_area* p_fa_dst = NULL;

    int rc = flash_area_open(fa_id_dst, &p_fa_dst);
    if (0 != rc)
    {
        LOG_ERR("Failed to open flash area %d, rc=%d", fa_id_dst, rc);
        return false;
    }

    rc                        = fs_seek(p_file_src, 0, FS_SEEK_END);
    const off_t src_file_size = fs_tell(p_file_src);
    rc                        = fs_seek(p_file_src, 0, FS_SEEK_SET);

    LOG_INF(
        "Copy %" PRIu32 " bytes from file to flash partition %d at offset 0x%08" PRIxPTR,
        (uint32_t)src_file_size,
        p_fa_dst->fa_id,
        p_fa_dst->fa_off);

    if (src_file_size > p_fa_dst->fa_size)
    {
        LOG_ERR(
            "File size: %" PRIu32 " is larger than partition size %" PRIu32,
            (uint32_t)src_file_size,
            (uint32_t)p_fa_dst->fa_size);
        return false;
    }

    if (flag_erase_dst)
    {
        rc = flash_area_erase(p_fa_dst, 0, p_fa_dst->fa_size);
        if (rc != 0)
        {
            LOG_ERR(
                "Failed to erase flash area %d (address 0x%08" PRIxPTR ", size 0x%08" PRIx32 "), rc=%d",
                fa_id_dst,
                (uintptr_t)p_fa_dst->fa_off,
                (uint32_t)p_fa_dst->fa_size,
                rc);
            return false;
        }
    }

    bool   is_success = true;
    size_t rem_len    = src_file_size;
    off_t  offset     = 0;
    while (rem_len > 0)
    {
        const size_t len = (rem_len > TMP_BUF_SIZE) ? TMP_BUF_SIZE : rem_len;

        rc = fs_read(p_file_src, tmp_buf1, len);
        if (rc < 0)
        {
            LOG_ERR("Failed to read file at offset 0x%08" PRIxPTR ", rc=%d", (uintptr_t)offset, rc);
            is_success = false;
            break;
        }
        if (rc != len)
        {
            LOG_ERR(
                "Failed to read file at offset 0x%08" PRIxPTR ", read %u bytes, expected %" PRIu32 " bytes",
                (uintptr_t)offset,
                (unsigned)rc,
                (uint32_t)len);
            is_success = false;
            break;
        }
        // len == 0, len % 4 = 0, padding = 0
        // len == 1, len % 4 = 1, padding = 3
        // len == 2, len % 4 = 2, padding = 2
        // len == 3, len % 4 = 3, padding = 1
        // len == 4, len % 4 = 0, padding = 0
        const size_t padding = (0 != (len % 4)) ? (4 - (len % 4)) : 0;
        if (0 != padding)
        {
            memset(&tmp_buf1[len], 0xFF, padding);
        }

        if (!cb_img_process(p_fa_dst, offset, tmp_buf1, len + padding))
        {
            is_success = false;
            break;
        }

        offset += len;
        rem_len -= len;
    }

    flash_area_close(p_fa_dst);
    return is_success;
}

static bool
cb_img_write(
    const struct flash_area* p_fa_dst,
    const off_t              offset,
    const uint8_t*           p_src_img_data_buf,
    const size_t             buf_len)
{
    int rc = flash_area_write(p_fa_dst, offset, p_src_img_data_buf, buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to write at address 0x%08x, rc=%d", (unsigned)(p_fa_dst->fa_off + offset), rc);
        return false;
    }
    return true;
}

static bool
cb_img_cmp(
    const struct flash_area* p_fa_dst,
    const off_t              offset,
    const uint8_t*           p_src_img_data_buf,
    const size_t             buf_len)
{
    static uint8_t tmp_buf2[TMP_BUF_SIZE];

    int rc = flash_area_read(p_fa_dst, offset, tmp_buf2, buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to read flash at address 0x%08x, rc=%d", (unsigned)(p_fa_dst->fa_off + offset), rc);
        return false;
    }

    if (memcmp(p_src_img_data_buf, tmp_buf2, buf_len) != 0)
    {
        LOG_INF("memcmp failed at address 0x%08x", (unsigned)(p_fa_dst->fa_off + offset));
        LOG_HEXDUMP_DBG(p_src_img_data_buf, buf_len, "src:");
        LOG_HEXDUMP_DBG(tmp_buf2, buf_len, "dst:");
        return false;
    }
    return true;
}

bool
mcuboot_img_op_copy(const int fa_id_dst, struct fs_file_t* const p_file_src)
{
    return img_process(fa_id_dst, p_file_src, true, &cb_img_write);
}

bool
mcuboot_img_op_cmp(const int fa_id_dst, struct fs_file_t* const p_file_src)
{
    return img_process(fa_id_dst, p_file_src, false, &cb_img_cmp);
}
