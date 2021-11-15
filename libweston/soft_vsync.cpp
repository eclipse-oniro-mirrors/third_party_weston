/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "soft_vsync.h"

#include <thread>
#include <unistd.h>

#include <hilog/log.h>
#include <vsync_module_c.h>

namespace {
void Log(const char* log)
{
    OHOS::HiviewDFX::HiLog::Info({LOG_CORE, 0, "WAYLAND_SERVER"}, "%{public}s", log);
}
} // namespace

SoftVsync &SoftVsync::GetInstance()
{
    static SoftVsync instance;
    return instance;
}

void SoftVsync::SoftVsyncThreadMain()
{

    int32_t ret;
    do {
        ret = VsyncModuleStart();
    } while (ret != 0);

    while (vsyncThreadRunning == true) {
        VsyncModuleTrigger();
        usleep(1e6 / 60);
    }
}

void SoftVsync::SoftVsyncStart()
{
    if (vsyncThreadRunning == false) {
        Log("start soft vsync thread");
        vsyncThreadRunning = true;
        auto threadMain = std::bind(&SoftVsync::SoftVsyncThreadMain, this);
        softVsyncThread = std::make_unique<std::thread>(threadMain);
    }
}

void SoftVsync::SoftVsyncStop()
{
    if (vsyncThreadRunning != true) {
        return;
    } else {
        Log("stop soft vsync thread");
        vsyncThreadRunning = false;
        softVsyncThread->join();
    }
}

void StartSoftVsyncThread()
{
    SoftVsync::GetInstance().SoftVsyncStart();
}

void StopSoftVsyncThread()
{
    SoftVsync::GetInstance().SoftVsyncStop();
}
