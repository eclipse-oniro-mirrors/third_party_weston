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

#ifndef LIBWESTON_SOFT_VSYNC_H
#define LIBWESTON_SOFT_VSYNC_H

#ifdef __cplusplus
#include <memory>
#include <thread>

class SoftVsync {
public:
    static SoftVsync &GetInstance();

    void SoftVsyncStart();
    void SoftVsyncStop();

private:
    SoftVsync() = default;
    ~SoftVsync() = default;

    void SoftVsyncThreadMain();

    std::unique_ptr<std::thread> softVsyncThread = nullptr;
    bool vsyncThreadRunning = false;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void StartSoftVsyncThread();
void StopSoftVsyncThread();

#ifdef __cplusplus
}
#endif

#endif // LIBWESTON_SOFT_VSYNC_H
