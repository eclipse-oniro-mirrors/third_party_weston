/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LIBWESTON_TRACE
#define LIBWESTON_TRACE

#include <stdint.h>

#define TRACE_ARGS(color) LABEL, __func__, __LINE__, "\033[" #color "m"
#define DEFINE_LOG_LABEL(str) static const char *LABEL = str

#define LOG_ENTERS(str) log_printf(TRACE_ARGS(33), "%s {", str); log_level_inc()
#define LOG_EXITS(str) log_level_dec(); log_printf(TRACE_ARGS(33), "} %s", str)
#define LOG_ENTER() LOG_ENTERS("")
#define LOG_EXIT() LOG_EXITS("")
#define LOG_INFO(fmt, ...) log_printf(TRACE_ARGS(36), fmt, ##__VA_ARGS__)
#define LOG_IMPORTANT(fmt, ...) log_printf(TRACE_ARGS(32), fmt, ##__VA_ARGS__)
#define LOG_CORE(fmt, ...) log_printf(TRACE_ARGS(35), "core: " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_printf(TRACE_ARGS(31), fmt, ##__VA_ARGS__)
#define LOG_PASS() log_printf(TRACE_ARGS(32), "pass")

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
void log_printf(const char *label, const char *func, int32_t line, const char *color, const char *fmt, ...);
void log_level_inc();
void log_level_dec();

#ifdef __cplusplus
}

#define LOG_SCOPE(...) ScopedLog log(LABEL, __func__, __LINE__, ##__VA_ARGS__)
class ScopedLog {
public:
    ScopedLog(const char *label, const char *func, int32_t line, const char *str = "");
    ~ScopedLog();

private:
    const char *label_;
    const char *func_;
    int32_t line_;
    const char *str_;
};
#endif

#endif // LIBWESTON_TRACE
