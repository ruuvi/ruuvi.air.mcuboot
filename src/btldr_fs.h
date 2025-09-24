/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef BTLDR_FS_H
#define BTLDR_FS_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
btldr_fs_flash_erase(void);

bool
btldr_fs_mount(void);

void
btldr_fs_unmount(void);

bool
btldr_fs_is_file_exist(const char* const p_file_name);

struct fs_file_t
btldr_fs_open_file(const char* const p_file_name);

void
btldr_fs_close_file(struct fs_file_t* const p_file);

bool
btldr_fs_unlink_file(const char* const p_file_name);

off_t
btldr_fs_get_file_size(struct fs_file_t* const p_file);

#ifdef __cplusplus
}
#endif

#endif // BTLDR_FS_H
