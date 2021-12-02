#ifndef LIBWESTON_TRACE
#define LIBWESTON_TRACE

#include <stdio.h>

#ifdef TRACE_ENABLE
#define _LOG(func, line, color, fmt, ...) \
    log_printf(LABEL, func, line, "\033[" #color "m" fmt, ##__VA_ARGS__)
#else
#define _LOG(func, line, color, fmt, ...) \
    log_printf(LABEL, func, line, fmt, ##__VA_ARGS__)
#endif

#define LOG_ENTER() log_enter(LABEL, __func__, __LINE__)
#define LOG_EXIT() log_exit(LABEL, __func__, __LINE__)
#define LOG_ENTERS(str) log_enters(LABEL, __func__, __LINE__, str)
#define LOG_EXITS(str) log_exits(LABEL, __func__, __LINE__, str)
#define LOG_INFO(fmt, ...) _LOG(__func__, __LINE__, 36, fmt, ##__VA_ARGS__)
#define LOG_CORE(fmt, ...) _LOG(__func__, __LINE__, 35, "core: " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) _LOG(__func__, __LINE__, 31, fmt, ##__VA_ARGS__)
#define LOG_PASS() _LOG(__func__, __LINE__, 32, "pass")
#define DEFINE_LOG_LABEL(str) static const char *LABEL = str;

#define LOG_REGION(note, region) \
        LOG_INFO(#note " " #region " (%d, %d) (%d, %d)", \
                (region)->extents.x1, (region)->extents.y1, \
                (region)->extents.x2, (region)->extents.y2)

#define LOG_MATRIX(matrix) \
    LOG_INFO(#matrix ": {"); \
    LOG_INFO(#matrix "    %f, %f, %f, %f", (matrix)->d[0], (matrix)->d[4], (matrix)->d[8], (matrix)->d[12]); \
    LOG_INFO(#matrix "    %f, %f, %f, %f", (matrix)->d[1], (matrix)->d[5], (matrix)->d[9], (matrix)->d[13]); \
    LOG_INFO(#matrix "    %f, %f, %f, %f", (matrix)->d[2], (matrix)->d[6], (matrix)->d[10], (matrix)->d[14]); \
    LOG_INFO(#matrix "    %f, %f, %f, %f", (matrix)->d[3], (matrix)->d[7], (matrix)->d[11], (matrix)->d[15]); \
    LOG_INFO(#matrix "}")

#ifdef __cplusplus
extern "C" {
#endif

void log_init();
void log_printf(const char *label, const char *func, int line, const char *fmt, ...);
void log_enter(const char *label, const char *func, int line);
void log_exit(const char *label, const char *func, int line);
void log_enters(const char *label, const char *func, int line, const char *str);
void log_exits(const char *label, const char *func, int line, const char *str);

#ifdef __cplusplus
}
#endif

#endif // LIBWESTON_TRACE
