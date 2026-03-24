#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define sphaira_USE_LOG 1

#include <stdarg.h>

#if sphaira_USE_LOG
bool log_file_init();
bool log_nxlink_init();
void log_file_exit();
bool log_is_init();

void log_nxlink_exit();
void log_write(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);
// Always writes to sphaira_boot.log regardless of whether logging is enabled in settings.
void log_write_boot(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
#else
inline bool log_file_init() {
    return true;
}
inline bool log_nxlink_init() {
    return true;
}
#define log_file_exit()
#define log_nxlink_exit()
#define log_write(...)
#define log_write_arg(...)
#define log_write_boot(...)
#endif

#ifdef __cplusplus
}
#endif
