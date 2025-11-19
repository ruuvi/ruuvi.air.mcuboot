/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include "mcuboot_segger_rtt.h"

#if (defined(CONFIG_USE_SEGGER_RTT) && defined(CONFIG_RTT_CONSOLE)) \
    && (defined(CONFIG_SERIAL) && defined(CONFIG_UART_CONSOLE))

#define SEGGER_RTT_WRAP_LOG_BUFFER_SIZE 192

__printf_like(1, 0) static void vprintk_to_segger_rtt(const char* fmt, va_list ap)
{
    char log_buffer[SEGGER_RTT_WRAP_LOG_BUFFER_SIZE];

    int32_t length = vsnprintf(log_buffer, sizeof(log_buffer), fmt, ap);

    if (length < 0)
    {
        return;
    }

    if (length >= sizeof(log_buffer))
    {
        // Output was truncated. Optionally add indicator like "...\n"
        const char trunc_msg[] = "...\r\n";
        _Static_assert(sizeof(log_buffer) > (sizeof(trunc_msg) - 1));
        const size_t trunc_msg_len = sizeof(trunc_msg) - 1;
        memcpy((log_buffer + sizeof(log_buffer)) - (1 + trunc_msg_len), trunc_msg, trunc_msg_len);
        log_buffer[sizeof(log_buffer) - 1] = '\0';
        length                             = (int32_t)sizeof(log_buffer) - 1;
    }

    if (length > 0)
    {
        mcuboot_segger_rtt_write(log_buffer, (uint32_t)length);
    }
}
#endif // CONFIG_USE_SEGGER_RTT && CONFIG_RTT_CONSOLE && CONFIG_SERIAL && CONFIG_UART_CONSOLE

__printf_like(1, 0) void __wrap_vprintk(const char* fmt, va_list ap) // NOSONAR
{
    // When both UART and RTT are enabled, we need to call SEGGER_RTT_Write manually,
    // becuse only one logging target is supported when `CONFIG_LOG_MODE_MINIMAL=y`.
#if (defined(CONFIG_USE_SEGGER_RTT) && defined(CONFIG_RTT_CONSOLE)) \
    && (defined(CONFIG_SERIAL) && defined(CONFIG_UART_CONSOLE))
    va_list args_copy; // Needed because vsnprintf might consume args
    va_copy(args_copy, ap);
    vprintk_to_segger_rtt(fmt, args_copy);
    va_end(args_copy);
#endif // defined(CONFIG_USE_SEGGER_RTT) && defined(CONFIG_RTT_CONSOLE)

    extern __printf_like(1, 0) void __real_vprintk(const char* fmt, va_list ap); // NOSONAR
    __real_vprintk(fmt, ap);
}
