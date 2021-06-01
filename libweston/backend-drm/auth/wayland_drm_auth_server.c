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
#include "wayland_drm_auth_server.h"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include "xf86drm.h"
#include "wayland_drm_auth.h"
#include "drm-auth-server-protocol.h"

typedef struct {
    int maserFd;
    struct wl_global *drmAuthGlobal;
} DrmAuthMng;

DrmAuthMng *g_drmAuthMng = NULL;

static void DrmAuthenticate(struct wl_client *client, struct wl_resource *resource, uint32_t magic)
{
    int ret;
    DrmAuthMng *drmAuthMng = wl_resource_get_user_data(resource);
    DRMAUTH_LOGD("DrmAuthenticate magic %x", magic);
    CHK_RETURN_NO_VALUE((drmAuthMng == NULL), DRMAUTH_LOGE("can not get user data"));
    ret = drmAuthMagic(drmAuthMng->maserFd, magic);
    if (ret) {
        DRMAUTH_LOGE("drm authenticate failed errno : %d", errno);
        wl_resource_post_event(resource, WL_DRM_AUTH_STATUS, WL_DRM_AUTH_STATUS_FAILED);
        return;
    }
    wl_resource_post_event(resource, WL_DRM_AUTH_STATUS, WL_DRM_AUTH_STATUS_SUCCESS);
}

static const struct wl_drm_auth_interface g_drmAuthInterface = { DrmAuthenticate };

static void BindDrmAuth(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource;
    DRMAUTH_LOGD("BindDrmAuth");
    resource = wl_resource_create(client, &wl_drm_auth_interface, 1, id);
    CHK_RETURN_NO_VALUE((resource == NULL), DRMAUTH_LOGE("create resource failed"); wl_client_post_no_memory(client));
    wl_resource_set_implementation(resource, &g_drmAuthInterface, data, NULL);
}

void DeInitWaylandDrmAuthService()
{
    DRMAUTH_LOGD("DeInitWaylandDrmAuthService");
    if (g_drmAuthMng->drmAuthGlobal != NULL) {
        wl_global_destroy(g_drmAuthMng->drmAuthGlobal);
    }
    if (g_drmAuthMng != NULL) {
        free(g_drmAuthMng);
        g_drmAuthMng = NULL;
    }
}

int InitWaylandDrmAuthService(struct wl_display *display, int drmMasterFd)
{
    if (g_drmAuthMng != NULL) {
        DRMAUTH_LOGI("drm auth service has inited will do nothing");
    }
    g_drmAuthMng = calloc(1, sizeof(DrmAuthMng));
    CHK_RETURN((g_drmAuthMng == NULL), -1, DRMAUTH_LOGE("calloc DrmAuthMng Failed errno : %d", errno));
    g_drmAuthMng->maserFd = drmMasterFd;
    g_drmAuthMng->drmAuthGlobal = wl_global_create(display, &wl_drm_auth_interface, 1, g_drmAuthMng, BindDrmAuth);
    CHK_RETURN((g_drmAuthMng == NULL), -1, DRMAUTH_LOGE("drm auth global create failed");
        DeInitWaylandDrmAuthService());
    return 0;
}
