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
#ifndef WAYLAND_DRM_AUTH_SERVER_H
#define WAYLAND_DRM_AUTH_SERVER_H

#include "wayland-server.h"

/* *
 * @brief init the wayland drm authenticate service
 *
 * it will create drm auth global to the wayland server, and handle the client authenticate resquest
 *
 * @param display Indicates the pointer of wayland display
 *
 * @param drmMasterFd Indicates the file descriptor of drm mast
 *
 * @return Returns <b>0</b> if the operation is successful; returns an error code defined in {@link DispErrCode}
 * otherwise.
 * @since 1.0
 * @version 1.0
 */
extern int InitWaylandDrmAuthService(struct wl_display *display, int drmMasterFd);

/* *
 * @brief deinit the wayland drm authenticate service
 *
 * it will destroy the drm auth global and free the memory of drm auth service
 *
 * otherwise.
 * @since 1.0
 * @version 1.0
 */
extern void DeInitWaylandDrmAuthService();

#endif // WAYLAND_DRM_AUTH_SERVER_H