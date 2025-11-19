/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(MCUBOOT_SEGGER_RTT_H)
#define MCUBOOT_SEGGER_RTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void
mcuboot_segger_rtt_check_data_location_and_size(void);

void
mcuboot_segger_rtt_write(const void* p_buffer, const uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // MCUBOOT_SEGGER_RTT_H
