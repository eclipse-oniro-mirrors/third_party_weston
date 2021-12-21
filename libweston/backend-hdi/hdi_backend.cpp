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
#include <sys/time.h>

#include <graphic_dumper_helper.h>
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
constexpr const char *dumper_view_tag = "weston.view";
constexpr const char *dumper_hdi_tag = "weston.hdi";
constexpr const char *dumper_vsync_tag = "weston.vsync";

struct hdi_backend *
to_hdi_backend(struct weston_compositor *base)
{
    return container_of(base->backend, struct hdi_backend, base);
}

static void
hdi_backend_plug_event(uint32_t device_id, bool connected, void *data)
{
    LOG_SCOPE();
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
}

static int
hdi_gl_renderer_init(struct hdi_backend *b)
{
   uint32_t format[3] = { b->gbm_format, 0, 0, };
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
    LOG_SCOPE();
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

    delete b;
}

static struct hdi_pending_state *
hdi_backend_create_pending_state(struct hdi_backend *b)
{
    auto hps = new struct hdi_pending_state();
    if (hps == NULL) {
        return NULL;
    }

    hps->backend = b;
    return hps;
}

static void
hdi_backend_destroy_pending_state(struct hdi_pending_state *hps)
{
    delete hps;
}

static void *
hdi_backend_repaint_begin(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_backend *b = to_hdi_backend(compositor);
    b->layer_dump_info_pending.clear();
    b->view_dump_info_pending.clear();
    return hdi_backend_create_pending_state(to_hdi_backend(compositor));
}

static int
hdi_backend_repaint_flush(struct weston_compositor *compositor,
                          void *repaint_data)
{
    LOG_SCOPE();
    struct hdi_backend *b = to_hdi_backend(compositor);
    auto hps = reinterpret_cast<struct hdi_pending_state *>(repaint_data);

    b->view_dump_info = b->view_dump_info_pending;
    b->layer_dump_info = b->layer_dump_info_pending;
    for (auto &[device_id, framebuffer] : hps->framebuffers) {
        bool needFlushFramebuffer = false;
        int32_t fence;
        int ret = b->device_funcs->PrepareDisplayLayers(device_id, &needFlushFramebuffer);
        LOG_CORE("[ret=%d] DeviceFuncs.PrepareDisplayLayers device_id: %d", ret, device_id);

        /* process comp change */
        // {
        //     uint32_t layer_numer;
        //     ret = b->device_funcs->GetDisplayCompChange(device_id, &layer_numer, NULL, NULL);
        //     LOG_CORE("[ret=%d] DeviceFuncs.GetDisplayCompChange", ret);
        //     LOG_INFO("change layer number: %d", layer_numer);
        //
        //     uint32_t *layers = calloc(layer_numer, sizeof *layers);
        //     CompositionType *types = calloc(layer_numer, sizeof *types);
        //     ret = b->device_funcs->GetDisplayCompChange(device_id, &layer_numer, layers, types);
        //     LOG_CORE("[ret=%d] DeviceFuncs.GetDisplayCompChange", ret);
        //     for (uint32_t i = 0; i < layer_numer; i++) {
        //         LOG_INFO("change layer id: %d, type: %d", layers[i], types[i]);
        //     }
        //     free(layers);
        //     free(types);
        // }

        if (needFlushFramebuffer == true) {
            ret = b->device_funcs->SetDisplayClientBuffer(device_id, framebuffer, -1);
            LOG_CORE("[ret=%d] DeviceFuncs.SetDisplayClientBuffer", ret);
        }

        gettimeofday(&b->samples[b->sample_current], nullptr);
        b->sample_current = (b->sample_current + 1) % (sizeof(b->samples) / sizeof(b->samples[0]));

        ret = b->device_funcs->Commit(device_id, &fence);
        LOG_CORE("[ret=%d] DeviceFuncs.Commit", ret);

        /* process release fence */
        // {
        //     uint32_t layer_numer;
        //     int ret = b->device_funcs->GetDisplayReleaseFence(device_id, &layer_numer, NULL, NULL);
        //     LOG_CORE("[ret=%d] DeviceFuncs.GetDisplayReleaseFence", ret);
        //     LOG_INFO("fence layer number: %d", layer_numer);
        //
        //     uint32_t *layers = calloc(layer_numer, sizeof *layers);
        //     int32_t *fences = calloc(layer_numer, sizeof *fences);
        //
        //     ret = b->device_funcs->GetDisplayReleaseFence(device_id, &layer_numer, layers, fences);
        //     LOG_CORE("[ret=%d] DeviceFuncs.GetDisplayReleaseFence", ret);
        //
        //     for (uint32_t i = 0; i < layer_numer; i++) {
        //         LOG_INFO("layer id: %d, fence: %d", layers[i], fences[i]);
        //     }
        // }
    }

    hdi_backend_destroy_pending_state(hps);
    return 0;
}

void OnDumpView(struct hdi_backend *b)
{
    auto dumper = OHOS::GraphicDumperHelper::GetInstance();
    for (auto &[device_id, res] : b->view_dump_info) {
        dumper->SendInfo(dumper_view_tag, "device_id: %d", device_id);
        for (auto &info : res) {
            dumper->SendInfo(dumper_view_tag, "    [%s]%p",
                    info.type == WESTON_RENDERER_TYPE_HDI ? "hdi" :
                    info.type == WESTON_RENDERER_TYPE_GPU ? "gpu" :
                    "unknown", info.view);
        }
    }

}

void OnDumpHdi(struct hdi_backend *b)
{
    auto dumper = OHOS::GraphicDumperHelper::GetInstance();
    for (auto &[device_id, res] : b->layer_dump_info) {
        dumper->SendInfo(dumper_hdi_tag, "device_id: %d", device_id);
        for (auto &[layer_id, info] : res) {
            dumper->SendInfo(dumper_hdi_tag, "    layer_id: %d", layer_id);
            dumper->SendInfo(dumper_hdi_tag, "        view %p", info.view);
            dumper->SendInfo(dumper_hdi_tag, "        src x: %d, y: %d, w: %d, h: %d",
                info.src.x, info.src.y, info.src.w, info.src.h);
            dumper->SendInfo(dumper_hdi_tag, "        dst x: %d, y: %d, w: %d, h: %d",
                info.dst.x, info.dst.y, info.dst.w, info.dst.h);
            dumper->SendInfo(dumper_hdi_tag, "        zorder: %d, blend: %d, composition: %d, transformMode: %d",
                info.zorder, info.blend_type, info.comp_type, info.rotate_type);
        }
    }
}

void OnDumpVsync(struct hdi_backend *b)
{
    auto &samples = b->samples;
    auto &sample_current = b->sample_current;
    auto framesize = sizeof(samples) / sizeof(samples[0]) - 1;

    auto dumper = OHOS::GraphicDumperHelper::GetInstance();
    auto last = (sample_current + framesize) % (framesize + 1);
    int64_t diff = (int64_t)samples[last].tv_sec * 1000000 + (int64_t)samples[last].tv_usec;
    diff -= (int64_t)samples[sample_current].tv_sec * 1000000 + (int64_t)samples[sample_current].tv_usec;
    if (diff == 0) {
        diff = 1;
    }

    double rate = 1000000.0 / diff * framesize;
    dumper->SendInfo(dumper_vsync_tag, "framerate: %lf", rate);
}

struct hdi_backend *
hdi_backend_create(struct weston_compositor *compositor,
            struct weston_hdi_backend_config *config)
{
    LOG_SCOPE();
    int ret;

    // ctor1. alloc memory
    auto b = new struct hdi_backend();
    if (b == NULL) {
        LOG_ERROR("zalloc hdi-backend failed");
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

    auto dumper = OHOS::GraphicDumperHelper::GetInstance();
    dumper->AddDumpListener(dumper_view_tag, std::bind(OnDumpView, b));
    dumper->AddDumpListener(dumper_hdi_tag, std::bind(OnDumpHdi, b));
    dumper->AddDumpListener(dumper_vsync_tag, std::bind(OnDumpVsync, b));

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
        LOG_ERROR("hdi_gl_renderer_init failed, gpu render disable.");
    }

    // init hdi device
    ret = DeviceInitialize(&b->device_funcs);
    LOG_CORE("[ret=%d] DeviceInitialize", ret);
    if (ret != DISPLAY_SUCCESS || b->device_funcs == NULL) {
        LOG_ERROR("DeviceInitialize failed");
        goto err_free;
    }

    ret = LayerInitialize(&b->layer_funcs);
    LOG_CORE("[ret=%d] LayerInitialize", ret);
    if (ret != DISPLAY_SUCCESS || b->layer_funcs == NULL) {
        LOG_ERROR("LayerInitialize failed");
        goto err_device_init;
    }

    b->display_gralloc = ::OHOS::HDI::Display::V1_0::IDisplayGralloc::Get();
    if (ret != DISPLAY_SUCCESS || b->display_gralloc == NULL) {
        LOG_ERROR("IDisplayGralloc::Get failed");
        goto err_layer_init;
    }

    do {
        const char *seat_id = "seat0";
        compositor->launcher = weston_launcher_connect(compositor, 1,
            seat_id, true);
        if (compositor->launcher == NULL) {
            LOG_ERROR("fatal: drm backend should be run using "
                      "weston-launch binary, or your system should "
                      "provide the logind D-Bus API.");
            break;
        }

        b->udev = udev_new();
        if (b->udev == NULL) {
            LOG_ERROR("failed to initialize udev context");
            break;
        }

        if (udev_input_init(&b->input, compositor,
                b->udev, "seat0", config->configure_device) < 0) {
            LOG_ERROR("failed to create input devices");
            break;
        }

        udev_input_enable(&b->input);
    } while (false);

    ret = b->device_funcs->RegHotPlugCallback(hdi_backend_plug_event, b);
    LOG_CORE("[ret=%d] DeviceFuncs.RegHotPlugCallback", ret);

    // init linux_dmabuf
    if (compositor->hdi_renderer->import_dmabuf) {
        if (linux_dmabuf_setup(compositor) < 0) {
            LOG_ERROR("Error: dmabuf protocol setup failed.\n");
            goto err_gralloc_init;
        }
    }

    // register plugin
    static const struct weston_hdi_output_api api = {
        hdi_output_set_mode,
    };
    if (weston_plugin_api_register(compositor,
        WESTON_HDI_OUTPUT_API_NAME, &api, sizeof(api)) < 0) {
        LOG_ERROR("Failed to register hdi output API.\n");
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
    delete b;
    return NULL;
}
