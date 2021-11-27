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

#include "trace.h"

#include <cstdarg>
#include <cstdio>
#include <memory.h>
#include <mutex>
#include <unistd.h>

#include <hilog/log.h>

using Cstr = const char *;

class IWestonTrace {
public:
    virtual ~IWestonTrace() = default;

    virtual void Output(Cstr label, Cstr func, int32_t line, Cstr color, Cstr str) = 0;
    virtual void LevelInc() = 0;
    virtual void LevelDec() = 0;
};

class WestonTraceImpl : public IWestonTrace {
public:
    WestonTraceImpl()
    {
        memset(space, ' ', sizeof(space));
        space[0] = 0;
    }

    virtual void Output(Cstr label, Cstr func, int32_t line, Cstr color, Cstr str) override
    {
        OHOS::HiviewDFX::HiLog::Info({(LogType)3, 0, "Weston"}, "\033[31m%{public}-10s |"
            " \033[33m%{public}-45s|\033[34m%{public}-5d\033[0m:%{public}s %s%{public}s\033[0m",
            label, func, line, space, color, str);
    }

    virtual void LevelInc() override
    {
        std::lock_guard<std::mutex> lock(levelSpaceMutex);
        space[level * 2] = ' ';
        level++;
        space[level * 2] = 0;
    }

    virtual void LevelDec() override
    {
        std::lock_guard<std::mutex> lock(levelSpaceMutex);
        space[level * 2] = ' ';
        level--;
        space[level * 2] = 0;
    }

private:
    int32_t level = 0;
    char space[64] = {};
    std::mutex levelSpaceMutex;
};

class WestonTraceNoop : public IWestonTrace {
public:
    virtual void Output(Cstr label, Cstr func, int32_t line, Cstr color, Cstr str) override
    {
        if (strcmp(color, "\033[33m") == 0) {
            return;
        }

        OHOS::HiviewDFX::HiLog::Info({(LogType)3, 0, "WAYLAND_SERVER"},
            "%{public}s/%{public}s<%{public}d>: %{public}s", label, func, line, str);
    }

    virtual void LevelInc() override
    {
    }

    virtual void LevelDec() override
    {
    }
};

static IWestonTrace *g_westonTrace = nullptr;

void log_init()
{
    if (access("/data/weston_trace", F_OK) == -1) {
        g_westonTrace = new WestonTraceNoop();
    } else {
        g_westonTrace = new WestonTraceImpl();
    }
}

void log_printf(Cstr label, Cstr func, int32_t line, Cstr color, Cstr fmt, ...)
{
    char str[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);
    g_westonTrace->Output(label, func, line, color, str);
}

void log_level_inc()
{
    g_westonTrace->LevelInc();
}

void log_level_dec()
{
    g_westonTrace->LevelDec();
}

ScopedLog::ScopedLog(Cstr label, Cstr func, int32_t line, Cstr str)
{
    label_ = label;
    func_ = func;
    line_ = line;
    str_ = str;
    log_printf(label_, func_, line_, "\033[33m %s {", str_);
    log_level_inc();
}

ScopedLog::~ScopedLog()
{
    log_level_dec();
    log_printf(label_, func_, line_, "\033[33m } %s", str_);
}
