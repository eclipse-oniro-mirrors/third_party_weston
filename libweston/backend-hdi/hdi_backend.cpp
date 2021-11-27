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


#include "config.h"

#include <assert.h>

#include <libudev.h>

#include "hdi_backend.h"
#include "hdi_head.h"
#include "hdi_output.h"
#include "hdi_renderer.h"

#include "mix_renderer.h"

// C header adapter
extern "C" {
#include "libweston/backend-hdi.h"
#include "libweston/launcher-util.h"
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include "libweston/linux-dmabuf.h"
#include "libweston/windowed-output-api.h"
#include "shared/helpers.h"
}

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiBackend");

struct hdi_backend *
to_hdi_backend(struct weston_compositor *base)
{
    return container_of(base->backend, struct hdi_backend, base);
}

static void
hdi_backend_plug_event(uint32_t device_id, bool connected, void *data)
{
    LOG_ENTER();
    struct hdi_backend *b = (struct hdi_backend *)data;

    if (connected == true) {
        LOG_INFO("new screen");
        hdi_head_create(b->compositor, device_id);
    } else {
        LOG_INFO("del screen");
        struct weston_head *base, *next;
        wl_list_for_each_safe(base, next, &b->compositor->head_list, compositor_link) {
            if (hdi_head_get_device_id(base) == device_id) {
                hdi_head_destroy(base);
            }
        }
    }
    LOG_EXIT();
}

static int
hdi_gl_renderer_init(struct hdi_backend *b)
{
   uint32_t format[3] = {
       b->gbm_format,
       0,
       0,
   };
   const struct gl_renderer_display_options options = {
       .egl_platform = EGL_PLATFORM_GBM_KHR,
       .egl_surface_type = EGL_PBUFFER_BIT,
       .drm_formats = format,
       .drm_formats_count = 2,
   };

   b->glri = (struct gl_renderer_interface *)weston_load_module("gl-renderer.so", "gl_renderer_interface");
   if (!b->glri)
       return -1;

   return b->glri->display_create(b->compositor, &options);
}

static void
hdi_backend_destroy(struct weston_compositor *ec)
{
    LOG_ENTER();
    struct hdi_backend *b = to_hdi_backend(ec);
    struct weston_head *base, *next;

    udev_input_destroy(&b->input);
    weston_compositor_shutdown(ec);

    wl_list_for_each_safe(base, next, &ec->head_list, compositor_link) {
        hdi_head_destroy(base);
    }

    delete b->display_gralloc;
    LayerUninitialize(b->layer_funcs);
    DeviceUninitialize(b->device_funcs);

    if (b->udev == NULL) {
        udev_unref(b->udev);
    }

    free(b);
    LOG_EXIT();
}

static struct hdi_pending_state *
hdi_backend_create_pending_state(struct hdi_backend *b)
{
    struct hdi_pending_state *hps =
        (struct hdi_pending_state *)zalloc(sizeof *hps);
    if (hps == NULL) {
        return NULL;
    }
    hps->backend = b;
    return hps;
}

static void
hdi_backend_destroy_pending_state(struct hdi_pending_state *hps)
{
    free(hps);
}

static void *
hdi_backend_repaint_begin(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_pending_state *hps =
        hdi_backend_create_pending_state(to_hdi_backend(compositor));
    return hps;
}

static int
hdi_backend_repaint_flush(struct weston_compositor *compositor,
                          void *repaint_data)
{
    LOG_ENTER();
    struct hdi_backend *b = to_hdi_backend(compositor);
    struct hdi_pending_state *hps = (struct hdi_pending_state *)repaint_data;

    if (hps->framebuffer == NULL) {
        hdi_backend_destroy_pending_state(hps);
        LOG_EXIT();
        return 0;
    }

    bool needFlushFramebuffer = false;
    int32_t fence;
    int ret = b->device_funcs->PrepareDisplayLayers(hps->device_id, &needFlushFramebuffer);
    LOG_CORE("DeviceFuncs.PrepareDisplayLayers return %d", ret);

    /* process comp change */
    // {
    //     uint32_t layer_numer;
    //     ret = b->device_funcs->GetDisplayCompChange(hps->device_id, &layer_numer, NULL, NULL);
    //     LOG_CORE("DeviceFuncs.GetDisplayCompChange return %d", ret);
    //     LOG_INFO("change layer number: %d", layer_numer);
    //
    //     uint32_t *layers = calloc(layer_numer, sizeof *layers);
    //     CompositionType *types = calloc(layer_numer, sizeof *types);
    //     ret = b->device_funcs->GetDisplayCompChange(hps->device_id, &layer_numer, layers, types);
    //     LOG_CORE("DeviceFuncs.GetDisplayCompChange return %d", ret);
    //     for (uint32_t i = 0; i < layer_numer; i++) {
    //         LOG_INFO("change layer id: %d, type: %d", layers[i], types[i]);
    //     }
    //     free(layers);
    //     free(types);
    // }

    if (needFlushFramebuffer == true) {
        ret = b->device_funcs->SetDisplayClientBuffer(hps->device_id, hps->framebuffer, -1);
        LOG_CORE("DeviceFuncs.SetDisplayClientBuffer return %d", ret);
    }
    ret = b->device_funcs->Commit(hps->device_id, &fence);
    LOG_CORE("DeviceFuncs.Commit return %d", ret);

    /* process release fence */
    // {
    //     uint32_t layer_numer;
    //     int ret = b->device_funcs->GetDisplayReleaseFence(hps->device_id, &layer_numer, NULL, NULL);
    //     LOG_CORE("DeviceFuncs.GetDisplayReleaseFence return %d", ret);
    //     LOG_INFO("fence layer number: %d", layer_numer);
    //
    //     uint32_t *layers = calloc(layer_numer, sizeof *layers);
    //     int32_t *fences = calloc(layer_numer, sizeof *fences);
    //
    //     ret = b->device_funcs->GetDisplayReleaseFence(hps->device_id, &layer_numer, layers, fences);
    //     LOG_CORE("DeviceFuncs.GetDisplayReleaseFence return %d", ret);
    //
    //     for (uint32_t i = 0; i < layer_numer; i++) {
    //         LOG_INFO("layer id: %d, fence: %d", layers[i], fences[i]);
    //     }
    // }
    hdi_backend_destroy_pending_state(hps);
    LOG_EXIT();
    return 0;
}

struct hdi_backend *
hdi_backend_create(struct weston_compositor *compositor,
            struct weston_hdi_backend_config *config)
{
    LOG_PASS();
    int ret;

    // ctor1. alloc memory
    struct hdi_backend *b = (struct hdi_backend *)zalloc(sizeof *b);
    if (b == NULL) {
        weston_log("zalloc hdi-backend failed");
        LOG_EXIT();
        return NULL;
    }

    // ctor2. init super attributes
    b->compositor = compositor;
    compositor->backend = &b->base;

    // ctor3. init virtual function
    b->base.destroy = hdi_backend_destroy;
    b->base.repaint_begin = hdi_backend_repaint_begin;
    b->base.repaint_flush = hdi_backend_repaint_flush;
    b->base.repaint_cancel = NULL;
    b->base.create_output = hdi_output_create;
    b->base.device_changed = NULL;
    b->base.can_scanout_dmabuf = NULL;

    // init renderer
    ret = mix_renderer_init(compositor);
    if (ret < 0) {
        LOG_ERROR("mix_renderer_init failed");
        goto err_free;
    }

    ret = hdi_renderer_init(compositor);
    if (ret < 0) {
        LOG_ERROR("hdi_renderer_init failed");
        goto err_free;
    }

    ret = hdi_gl_renderer_init(b);
    if (ret < 0) {
        weston_log("hdi_gl_renderer_init failed, gpu render disable.");
    }

    // init hdi device
    ret = DeviceInitialize(&b->device_funcs);
    LOG_CORE("DeviceInitialize return %d", ret);
    if (ret != DISPLAY_SUCCESS || b->device_funcs == NULL) {
        weston_log("DeviceInitialize failed");
        goto err_free;
    }

    ret = LayerInitialize(&b->layer_funcs);
    LOG_CORE("LayerInitialize return %d", ret);
    if (ret != DISPLAY_SUCCESS || b->layer_funcs == NULL) {
        weston_log("LayerInitialize failed");
        goto err_device_init;
    }

    b->display_gralloc = ::OHOS::HDI::Display::V1_0::IDisplayGralloc::Get();
    if (ret != DISPLAY_SUCCESS || b->display_gralloc == NULL) {
        weston_log("IDisplayGralloc::Get failed");
        goto err_layer_init;
    }

    do {
        const char *seat_id = "seat0";
        compositor->launcher = weston_launcher_connect(compositor, 1,
            seat_id, true);
        if (compositor->launcher == NULL) {
            weston_log("fatal: drm backend should be run using "
                       "weston-launch binary, or your system should "
                       "provide the logind D-Bus API.");
            break;
        }

        b->udev = udev_new();
        if (b->udev == NULL) {
            weston_log("failed to initialize udev context");
            break;
        }

        if (udev_input_init(&b->input, compositor,
                b->udev, "seat0", config->configure_device) < 0) {
            weston_log("failed to create input devices");
            break;
        }

        udev_input_enable(&b->input);
    } while (false);

    ret = b->device_funcs->RegHotPlugCallback(hdi_backend_plug_event, b);
    LOG_CORE("DeviceFuncs.RegHotPlugCallback return %d", ret);

    // init linux_dmabuf
    if (compositor->hdi_renderer->import_dmabuf) {
        if (linux_dmabuf_setup(compositor) < 0) {
            weston_log("Error: dmabuf protocol setup failed.\n");
            goto err_gralloc_init;
        }
    }

    // register plugin
    static const struct weston_hdi_output_api api = {
        hdi_output_set_mode,
    };
    if (weston_plugin_api_register(compositor,
        WESTON_HDI_OUTPUT_API_NAME, &api, sizeof(api)) < 0) {
        weston_log("Failed to register hdi output API.\n");
        goto err_gralloc_init;
    }

    return b;

err_gralloc_init:
    delete b->display_gralloc;

err_layer_init:
    LayerUninitialize(b->layer_funcs);

err_device_init:
    DeviceUninitialize(b->device_funcs);

err_free:
    weston_compositor_shutdown(compositor);
    free(b);
    return NULL;
}
