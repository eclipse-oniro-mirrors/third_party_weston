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
#ifndef WAYLAND_DRM_AUTH_H
#define WAYLAND_DRM_AUTH_H
#include <string.h>
#include "libweston/libweston.h"

#undef LOG_TAG
#undef LOG_DOMAIN
#define LOG_TAG "DRMAUTH"
#define LOG_DOMAIN 0xD001400

#define FILENAME (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)

#ifndef DRMAUTH_LOGD
#define DRMAUTH_LOGD(format, ...)                                                                \
    do {                                                                                         \
        HILOG_DEBUG("[%s@%s:%d] " format "\n", __FUNCTION__, FILENAME, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif

#ifndef DRMAUTH_LOGI
#define DRMAUTH_LOGI(format, ...)                                                               \
    do {                                                                                        \
        HILOG_INFO("[%s@%s:%d] " format "\n", __FUNCTION__, FILENAME, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif

#ifndef DRMAUTH_LOGW
#define DRMAUTH_LOGW(format, ...)                                                               \
    do {                                                                                        \
        HILOG_WARN("[%s@%s:%d] " format "\n", __FUNCTION__, FILENAME, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif

#ifndef DRMAUTH_LOGE
#define DRMAUTH_LOGE(format, ...)                                                                \
    do {                                                                                         \
        HILOG_ERROR("[%s@%s:%d] " format "\n", __FUNCTION__, FILENAME, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif


#ifndef CHK_RETURN
#define CHK_RETURN(val, ret, ...) \
    do {                          \
        if (val) {                \
            __VA_ARGS__;          \
            return (ret);         \
        }                         \
    } while (0)
#endif

#ifndef CHK_RETURN_NO_VALUE
#define CHK_RETURN_NO_VALUE(val, ret, ...) \
    do {                                   \
        if (val) {                         \
            __VA_ARGS__;                   \
            return;                        \
        }                                  \
    } while (0)
#endif

#endif // WAYLAND_DRM_AUTH_H
