#include "trace.h"

#include <hilog/log.h>
#include <memory.h>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>

#ifdef TRACE_ENABLE
static int level = 0;
static char space[20];
static std::mutex levelSpaceMutex;
#endif

void log_init()
{
}

void log_printf(const char *label, const char *func, int line, const char *fmt, ...)
{
    char str[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);
#ifdef TRACE_ENABLE
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    OHOS::HiviewDFX::HiLog::Info({(LogType)3, 0, "Weston"}, "\033[31m%{public}-10s |"
        " \033[33m%{public}-45s|\033[34m%{public}-5d\033[0m:%{public}s %{public}s\033[0m",
        label, func, line, space, str);
#else
    OHOS::HiviewDFX::HiLog::Info({(LogType)3, 0, "WAYLAND_SERVER"},
        "%{public}s/%{public}s: %{public}s", label, func, str);
#endif
}

void log_enter(const char *label, const char *func, int line)
{
    log_enters(label, func, line, "");
}

void log_exit(const char *label, const char *func, int line)
{
    log_exits(label, func, line, "");
}

void log_enters(const char *LABEL, const char *func, int line, const char *str)
{
#ifdef TRACE_ENABLE
    std::lock_guard<std::mutex> lock(levelSpaceMutex);
    _LOG(func, line, 33, "{ %s", str);
    level++;
    memset(space, ' ', sizeof(space));
    space[level * 2] = 0;
#endif
}

void log_exits(const char *LABEL, const char *func, int line, const char *str)
{
#ifdef TRACE_ENABLE
    std::lock_guard<std::mutex> lock(levelSpaceMutex);
    level--;
    memset(space, ' ', sizeof(space));
    space[level * 2] = 0;
    _LOG(func, line, 33, "} %s", str);
#endif
}
