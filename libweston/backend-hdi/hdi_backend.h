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

#ifndef LIBWESTON_BACKEND_HDI_HDI_BACKEND_H
#define LIBWESTON_BACKEND_HDI_HDI_BACKEND_H

#include <display_device.h>
#include <display_layer.h>
#include <idisplay_gralloc.h>

#ifdef __cplusplus
#include <map>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "libinput-seat.h"
#include "libweston/libweston.h"
#include "libweston/backend.h"
#include "linux-dmabuf.h"
#include "shared/weston-egl-ext.h"
#include "renderer-gl/gl-renderer.h"
struct weston_hdi_backend_config;

enum hdi_renderer_type {
    HDI_RENDERER_HDI,
};

struct hdi_backend {
    struct weston_backend base;
    struct weston_compositor *compositor;
    enum hdi_renderer_type renderer_type;
    DeviceFuncs *device_funcs;
    LayerFuncs *layer_funcs;
    ::OHOS::HDI::Display::V1_0::IDisplayGralloc *display_gralloc;
    struct udev_input input;
    struct udev *udev;
    struct gl_renderer_interface *glri;
    uint32_t gbm_format;
};

struct hdi_pending_state {
    struct hdi_backend *backend;
#ifdef __cplusplus
    std::map<uint32_t, BufferHandle *> framebuffers;
#endif
};

struct hdi_backend *
to_hdi_backend(struct weston_compositor *base);

struct hdi_backend *
hdi_backend_create(struct weston_compositor *compositor,
            struct weston_hdi_backend_config *config);

#ifdef __cplusplus
}
#endif

#endif // LIBWESTON_BACKEND_HDI_HDI_BACKEND_H
