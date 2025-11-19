/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "btldr_fs.h"
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include "ruuvi_fw_update.h"
#include "ruuvi_fa_id.h"
#include "zephyr_api.h"

LOG_MODULE_REGISTER(btldr_fs, LOG_LEVEL_INF);

typedef struct btldr_fs_abs_path_t
{
    char buf[MAX_FILE_NAME + 1];
} btldr_fs_abs_path_t;

static K_MUTEX_DEFINE(g_btldr_fs_mutex);
static btldr_fs_abs_path_t g_btldr_fs_abs_path;
static struct fs_dirent    g_btldr_fs_dir_entry;

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t btldr_fs_storage_mnt = {
    .type        = FS_LITTLEFS,
    .fs_data     = &storage,
    .storage_dev = (void*)FIXED_PARTITION_ID(littlefs_storage1),
    .mnt_point   = RUUVI_FW_UPDATE_MOUNT_POINT,
};

static struct fs_mount_t* const g_mountpoint = &btldr_fs_storage_mnt;

static const btldr_fs_abs_path_t*
btldr_fs_lock_and_get_abs_path(const char* const p_rel_file_name)
{
    k_mutex_lock(&g_btldr_fs_mutex, K_FOREVER);
    snprintf(
        g_btldr_fs_abs_path.buf,
        sizeof(g_btldr_fs_abs_path.buf),
        "%s/%s",
        g_mountpoint->mnt_point,
        p_rel_file_name);
    return &g_btldr_fs_abs_path;
}

static void
btldr_fs_unlock(void)
{
    k_mutex_unlock(&g_btldr_fs_mutex);
}

bool
btldr_fs_flash_erase(void)
{
    const fa_id_t            btldr_fs_fa_id = PM_ID(littlefs_storage1);
    const struct flash_area* pfa            = NULL;

    LOG_INF("Erase %s (storage_dev %p)", g_mountpoint->mnt_point, g_mountpoint->storage_dev);

    zephyr_api_ret_t rc = flash_area_open(btldr_fs_fa_id, &pfa);
    if (rc < 0)
    {
        LOG_ERR("FAIL: unable to find flash area %u: %d", btldr_fs_fa_id, rc);
        return false;
    }

    LOG_INF(
        "Area %u at 0x%08" PRIx32 " on %s for %" PRIu32 " bytes",
        btldr_fs_fa_id,
        (uint32_t)pfa->fa_off,
        pfa->fa_dev->name,
        (uint32_t)pfa->fa_size);

    LOG_INF("Erasing 'littlefs_storage1' flash area (id=%d)...", btldr_fs_fa_id);
    rc = flash_area_flatten(pfa, 0, pfa->fa_size);
    if (rc < 0)
    {
        LOG_ERR("Erasing flash area failed, rc=%d", rc);
        flash_area_close(pfa);
        return false;
    }
    LOG_INF("Erasing flash area finished successfully");

    flash_area_close(pfa);
    return true;
}

bool
btldr_fs_mount(void)
{
    zephyr_api_ret_t rc = fs_mount(g_mountpoint);
    if (0 != rc)
    {
        LOG_ERR(
            "FAIL: mount id %" PRIuPTR " at %s: %d",
            (uintptr_t)g_mountpoint->storage_dev,
            g_mountpoint->mnt_point,
            rc);
        btldr_fs_flash_erase();
        return false;
    }
    LOG_INF("%s mounted successfully", g_mountpoint->mnt_point);

    struct fs_statvfs sbuf = { 0 };
    rc                     = fs_statvfs(g_mountpoint->mnt_point, &sbuf);
    if (rc < 0)
    {
        LOG_ERR("FAIL: statvfs: %d", rc);
        return false;
    }
    LOG_INF(
        "%s: bsize = %lu ; frsize = %lu ; blocks = %lu ; bfree = %lu",
        g_mountpoint->mnt_point,
        sbuf.f_bsize,
        sbuf.f_frsize,
        sbuf.f_blocks,
        sbuf.f_bfree);

    return true;
}

void
btldr_fs_unmount(void)
{
    const zephyr_api_ret_t rc = fs_unmount(g_mountpoint);
    if (0 != rc)
    {
        LOG_ERR("FAIL: unmount %s: rc=%d", g_mountpoint->mnt_point, rc);
    }
    else
    {
        LOG_INF("%s unmounted sucessfully", g_mountpoint->mnt_point);
    }
}

bool
btldr_fs_is_file_exist(const char* const p_file_name)
{
    const btldr_fs_abs_path_t* const p_abs_path = btldr_fs_lock_and_get_abs_path(p_file_name);

    bool                   res = true;
    const zephyr_api_ret_t rc  = fs_stat(p_abs_path->buf, &g_btldr_fs_dir_entry);
    if (-ENOENT == rc)
    {
        res = false;
    }
    else if (0 != rc)
    {
        LOG_ERR("Failed to stat file %s, rc=%d", p_file_name, rc);
        res = false;
    }
    else if (FS_DIR_ENTRY_FILE != g_btldr_fs_dir_entry.type)
    {
        LOG_ERR("File %s is not a file", p_file_name);
        res = false;
    }
    else
    {
        // MISRA: "if ... else if" constructs should end with "else" clauses
    }
    btldr_fs_unlock();
    return res;
}

struct fs_file_t
btldr_fs_open_file(const char* const p_file_name)
{
    const btldr_fs_abs_path_t* const p_abs_path = btldr_fs_lock_and_get_abs_path(p_file_name);

    struct fs_file_t file = { 0 };
    fs_file_t_init(&file);
    const zephyr_api_ret_t rc = fs_open(&file, p_abs_path->buf, FS_O_READ);
    if (rc < 0)
    {
        LOG_ERR("Failed to open file %s, rc=%d", p_file_name, rc);
        file.filep = NULL;
    }
    btldr_fs_unlock();
    return file;
}

void
btldr_fs_close_file(struct fs_file_t* const p_file)
{
    fs_close(p_file);
}

bool
btldr_fs_unlink_file(const char* const p_file_name)
{
    const btldr_fs_abs_path_t* const p_abs_path = btldr_fs_lock_and_get_abs_path(p_file_name);

    LOG_INF("Remove file: %s", p_file_name);
    bool                   res = true;
    const zephyr_api_ret_t rc  = fs_unlink(p_abs_path->buf);
    if (rc < 0)
    {
        LOG_ERR("Failed to unlink file %s, rc=%d", p_file_name, rc);
        res = false;
    }
    btldr_fs_unlock();
    return res;
}

off_t
btldr_fs_get_file_size(struct fs_file_t* const p_file)
{
    const off_t      cur_offset = fs_tell(p_file);
    zephyr_api_ret_t rc         = fs_seek(p_file, 0, FS_SEEK_END);
    if (0 != rc)
    {
        LOG_ERR("Failed to get file size, rc=%d", rc);
        return 0;
    }
    const off_t size = fs_tell(p_file);
    (void)fs_seek(p_file, cur_offset, FS_SEEK_SET);
    return size;
}
