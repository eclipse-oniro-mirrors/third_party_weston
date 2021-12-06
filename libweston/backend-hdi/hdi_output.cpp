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

#include <cassert>
#include <iomanip>
#include <list>
#include <sstream>

#include <display_type.h>

#include "hdi_backend.h"
#include "hdi_head.h"
#include "hdi_output.h"
#include "hdi_renderer.h"

// C header adapter
extern "C" {
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include "libweston/pixman-renderer.h"
#include "shared/helpers.h"
}

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiOutput");

#define HDI_OUTPUT_FRMAEBUFFER_SIZE 2
#define HDI_OUTPUT_FRMAEBUFFER_GL_SIZE 2

struct hdi_output {
    struct weston_output base;
    struct weston_mode mode;
    BufferHandle *hdi_framebuffers[HDI_OUTPUT_FRMAEBUFFER_SIZE];
    BufferHandle *gl_framebuffers[HDI_OUTPUT_FRMAEBUFFER_GL_SIZE];
    uint32_t current_framebuffer_id;
    struct wl_event_source *finish_frame_timer;
    BufferHandle *framebuffer; // used by read_pixels
};

static struct hdi_output *
to_hdi_output(struct weston_output *base)
{
    return container_of(base, struct hdi_output, base);
}

static int
hdi_output_start_repaint_loop(struct weston_output *output)
{
    LOG_SCOPE();
    struct timespec ts;
    weston_compositor_read_presentation_clock(output->compositor, &ts);
    weston_output_finish_frame(output, &ts, WP_PRESENTATION_FEEDBACK_INVALID);
    return 0;
}

static int
hdi_finish_frame_handle(void *data)
{
    LOG_CORE("finish_frame_timer called");
    struct hdi_output *output =
        reinterpret_cast<struct hdi_output *>(data);
    struct timespec ts;
    weston_compositor_read_presentation_clock(output->base.compositor, &ts);
    weston_output_finish_frame(&output->base, &ts, 0);
    return 1;
}

static void
hdi_output_active_timer(struct hdi_output *output)
{
    LOG_CORE("active finish_frame_timer");
    wl_event_source_timer_update(output->finish_frame_timer,
                                 1000000 / output->mode.refresh);
}

static void
hdi_output_create_timer(struct hdi_output *output)
{
    struct wl_event_loop *loop =
        wl_display_get_event_loop(output->base.compositor->wl_display);
    output->finish_frame_timer =
        wl_event_loop_add_timer(loop, hdi_finish_frame_handle, output);
}

static void
hdi_output_destroy_timer(struct hdi_output *output)
{
    wl_event_source_remove(output->finish_frame_timer);
    output->finish_frame_timer = NULL;
}

std::ostream &operator <<(std::ostream &os, const enum weston_renderer_type &type)
{
    switch (type) {
        case WESTON_RENDERER_TYPE_GPU:
            os << "GPU";
            break;
        case WESTON_RENDERER_TYPE_HDI:
            os << "HDI";
            break;
    }
    return os;
}

static void dump_to_file(struct weston_buffer *buffer)
{
    static int32_t cnt = 0;
    static int64_t last = 0;
    cnt++;
    int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (access("/data/render_dump", F_OK) == -1) {
        last = now;
        return;
    }

    if (buffer == nullptr) {
        LOG_ERROR("buffer is nullptr");
        return;
    }

    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (dmabuf == nullptr) {
        LOG_ERROR("buffer->resource is not dmabuf");
        return;
    }

    BufferHandle *bh = dmabuf->attributes.buffer_handle;
    if (bh == NULL) {
        LOG_ERROR("dmabuf have no BufferHandle");
        return;
    }

    double diff = now - last;
    if (diff > 1e10) {
        diff = 0;
    }
    last = now;

    const char *unit = nullptr;
    if (diff > 1e9) {
        unit = "s";
        diff /= 1e9;
    } else if (diff > 1e6) {
        unit = "ms";
        diff /= 1e6;
    } else if (diff > 1e3) {
        unit = "us";
        diff /= 1e3;
    } else {
        unit = "ns";
    }

    std::stringstream ss;
    ss << "/data/render_" << cnt << "_" << std::setprecision(3) << diff << unit << ".raw";
    LOG_INFO("dumpimage: %s [fd=%d] (%dx%d)=%d [format=%d] [usage=%" PRIu64 "] {%" PRIu64 " -> %p}",
        ss.str().c_str(), bh->fd, bh->width, bh->height,
        bh->size, bh->format, bh->usage, bh->virAddr, bh->phyAddr);

    auto fp = fopen(ss.str().c_str(), "a+");
    if (fp == nullptr) {
        return;
    }

    fwrite(bh->virAddr, bh->size, 1, fp);
    fclose(fp);
}

BufferHandle *
hdi_output_get_framebuffer(struct weston_output *output)
{
    if (output == nullptr) {
        return nullptr;
    }
    return to_hdi_output(output)->framebuffer;
}

static int
hdi_output_repaint(struct weston_output *output_base,
                   pixman_region32_t *damage,
                   void *repaint_data)
{
    LOG_SCOPE();
    struct hdi_pending_state *hps =
        reinterpret_cast<struct hdi_pending_state *>(repaint_data);
    struct hdi_output *output = to_hdi_output(output_base);
    struct weston_head *h = weston_output_get_first_head(output_base);
    auto device_id = hdi_head_get_device_id(h);

    // prepare framebuffer
    output->current_framebuffer_id = (output->current_framebuffer_id + 1) % 2;
    auto &hdi_framebuffer = output->hdi_framebuffers[output->current_framebuffer_id];
    auto &gl_framebuffer = output->gl_framebuffers[output->current_framebuffer_id];

    // assign view to renderer
    bool need_gpu_render = false;
    bool need_hdi_render = false;
    std::list<std::stringstream> sss;
    int32_t cnt = 0;
    struct weston_view *view;
    wl_list_for_each_reverse(view, &output_base->compositor->view_list, link) {
        if (cnt++ % 5 == 0) {
            sss.emplace_back();
            sss.back() << "view_list:";
        }
        sss.back() << " [" << view->renderer_type << "]" << (void *)view << ",";

        if (view->renderer_type == WESTON_RENDERER_TYPE_GPU) {
            need_gpu_render = true;
        } else if (view->renderer_type == WESTON_RENDERER_TYPE_HDI) {
            need_hdi_render = true;
        }

        if (view->surface->type != WL_SURFACE_TYPE_VIDEO) {
            dump_to_file(view->surface->buffer_ref.buffer);
        }
    }

    LOG_IMPORTANT("device_id: %d", device_id);
    for (const auto &ss : sss) {
        LOG_INFO("%s", ss.str().c_str());
    }

    // gpu render
    if (need_gpu_render) {
        output_base->compositor->gpu_renderer->repaint_output(output_base, damage);
        if (need_hdi_render) {
            hdi_renderer_output_set_gpu_buffer(output_base, gl_framebuffer);
        }
    }

    // hdi render
    if (need_hdi_render) {
        output_base->compositor->hdi_renderer->repaint_output(output_base, damage);
        hps->framebuffers[device_id] = hdi_framebuffer;
    } else {
        hps->framebuffers[device_id] = gl_framebuffer;
    }

    // ScreenShot
    output->framebuffer = hps->framebuffers[device_id];
    switch (output->framebuffer->format) {
        case PIXEL_FMT_RGBA_8888:
            output_base->compositor->read_format = PIXMAN_a8b8g8r8;
            break;
        case PIXEL_FMT_BGRA_8888:
            output_base->compositor->read_format = PIXMAN_a8r8g8b8;
            break;
        default:
            assert(!"not support format");
            break;
    }
    wl_signal_emit(&output_base->frame_signal, damage);

    hdi_output_active_timer(output);
    return 0;
}

int
hdi_output_set_mode(struct weston_output *base)
{
    LOG_SCOPE();
    struct hdi_output *output = to_hdi_output(base);
    int output_width, output_height;

    /* We can only be called once. */
    assert(!output->base.current_mode);

    /* Make sure we have scale set. */
    assert(output->base.scale);

    struct hdi_backend *b = to_hdi_backend(base->compositor);
    struct weston_head *whead = weston_output_get_first_head(&output->base);
    uint32_t device_id = hdi_head_get_device_id(whead);

    int mode_number = 0;
    int ret = b->device_funcs->GetDisplaySuppportedModes(device_id, &mode_number, NULL);
    LOG_CORE("DeviceFuncs.GetDisplaySuppportedModes return %d", ret);

    DisplayModeInfo *modes =
        reinterpret_cast<DisplayModeInfo *>(zalloc(mode_number * sizeof(DisplayModeInfo)));
    ret = b->device_funcs->GetDisplaySuppportedModes(device_id, &mode_number, modes);
    LOG_CORE("DeviceFuncs.GetDisplaySuppportedModes return %d", ret);
    LOG_INFO("%s support %d modes", base->name, mode_number);

    uint32_t active_mode_id;
    ret = b->device_funcs->GetDisplayMode(device_id, &active_mode_id);
    LOG_CORE("DeviceFuncs.GetDisplayMode return %d", ret);

    int width = 0;
    int height = 0;
    int fresh_rate = 60;
    for (int i = 0; i < mode_number; i++) {
        LOG_INFO("modes(%d) %dx%d %dHz", modes[i].id, modes[i].width, modes[i].height, modes[i].freshRate);
        if (modes[i].id == active_mode_id) {
            width = modes[i].width;
            height = modes[i].height;
            fresh_rate = modes[i].freshRate;
        }
    }

    weston_head_set_monitor_strings(whead, "weston", "hdi", NULL);
    weston_head_set_physical_size(whead, width, height);

    output->mode.flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
    output->mode.width = width * output->base.scale;
    output->mode.height = height * output->base.scale;
    output->mode.refresh = fresh_rate * 1000;
    wl_list_insert(&output->base.mode_list, &output->mode.link);

    output->base.current_mode = &output->mode;
    output->base.width = output->base.current_mode->width;
    output->base.height = output->base.current_mode->height;
    LOG_INFO("%s now use %d mode, %dx%d %dHz",
             base->name, active_mode_id,
             output->base.width, output->base.height, fresh_rate);

    return 0;
}

static void
hdi_output_assign_planes(struct weston_output *output_base,
                         void *repaint_data)
{
    struct weston_view *view;
    wl_list_for_each(view, &output_base->compositor->view_list, link) {
        weston_view_move_to_plane(view, &output_base->compositor->primary_plane);
        view->psf_flags = 0;
        view->surface->keep_buffer = true;
    }
}

static int
hdi_output_enable(struct weston_output *base)
{
    LOG_SCOPE();
    struct hdi_output *output = to_hdi_output(base);
    struct hdi_backend *b = to_hdi_backend(base->compositor);
    struct gl_renderer_fbo_options fbo_options;

    AllocInfo info = {
        .width = output->mode.width,
        .height = output->mode.height,
        .usage = HBM_USE_MEM_DMA | HBM_USE_CPU_READ | HBM_USE_CPU_WRITE,
        .format = PIXEL_FMT_BGRA_8888,
    };
    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_SIZE; i++) {
        int ret = b->display_gralloc->AllocMem(info, output->hdi_framebuffers[i]);
        LOG_CORE("GrallocFuncs.AllocMem return %d", ret);
        b->display_gralloc->Mmap(*output->hdi_framebuffers[i]);
        LOG_CORE("GrallocFuncs.Mmap return %p", output->hdi_framebuffers[i]->virAddr);
    }

    info.format = PIXEL_FMT_RGBA_8888; // gl renderer color format
    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_GL_SIZE; i++) {
        int ret = b->display_gralloc->AllocMem(info, output->gl_framebuffers[i]);
        LOG_CORE("GrallocFuncs.AllocMem return %d", ret);
        b->display_gralloc->Mmap(*output->gl_framebuffers[i]);
        LOG_CORE("GrallocFuncs.Mmap return %p", output->gl_framebuffers[i]->virAddr);

        fbo_options.handle[i] = output->gl_framebuffers[i];
    }

    if (base->compositor->gpu_renderer) {
        b->glri->output_fbo_create(base, &fbo_options);
    }

    output->base.start_repaint_loop = hdi_output_start_repaint_loop;
    output->base.repaint = hdi_output_repaint;
    output->base.assign_planes = hdi_output_assign_planes;
    output->base.set_dpms = NULL;
    output->base.switch_mode = NULL;
    output->base.set_gamma = NULL;
    output->base.set_backlight = NULL;
    output->current_framebuffer_id = 0;
    hdi_renderer_output_create(base, NULL);
    hdi_output_create_timer(output);
    hdi_output_active_timer(output);
    return 0;
}

static int
hdi_output_disable(struct weston_output *base)
{
    LOG_SCOPE();
    struct hdi_output *output = to_hdi_output(base);
    struct hdi_backend *b = to_hdi_backend(base->compositor);

    if (!base->enabled) {
        return 0;
    }

    hdi_output_destroy_timer(output);
    hdi_renderer_output_destroy(base);

    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_SIZE; i++) {
        int ret = b->display_gralloc->Unmap(*output->hdi_framebuffers[i]);
        LOG_CORE("GrallocFuncs.Unmap hdi_framebuffers");
        b->display_gralloc->FreeMem(*output->hdi_framebuffers[i]);
        LOG_CORE("GrallocFuncs.FreeMem hdi_framebuffers");
    }

    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_GL_SIZE; i++) {
        int ret = b->display_gralloc->Unmap(*output->gl_framebuffers[i]);
        LOG_CORE("GrallocFuncs.Unmap gl_framebuffers");
        b->display_gralloc->FreeMem(*output->gl_framebuffers[i]);
        LOG_CORE("GrallocFuncs.FreeMem gl_framebuffers");
    }
    return 0;
}

static void
hdi_output_destroy(struct weston_output *base)
{
    LOG_SCOPE();
    hdi_output_disable(base);
    weston_output_release(base);
    free(to_hdi_output(base));
}

static int
hdi_output_attach_head(struct weston_output *output_base,
                       struct weston_head *head_base)
{
    LOG_SCOPE();
    if (output_base->enabled == false) {
        return 0;
    }

    weston_output_schedule_repaint(output_base);
    return 0;
}

static void
hdi_output_detach_head(struct weston_output *output_base,
                       struct weston_head *head_base)
{
    LOG_SCOPE();
    if (output_base->enabled == false) {
        return;
    }

    weston_output_schedule_repaint(output_base);
}

struct weston_output *
hdi_output_create(struct weston_compositor *compositor, const char *name)
{
    LOG_SCOPE();
    assert(name && !"name cannot be NULL.");

    struct hdi_output *output = (struct hdi_output *)zalloc(sizeof *output);
    if (!output) {
        LOG_ERROR("zalloc hdi_output failed");
        return NULL;
    }

    weston_output_init(&output->base, compositor, name);

    output->base.enable = hdi_output_enable;
    output->base.destroy = hdi_output_destroy;
    output->base.disable = hdi_output_disable;
    output->base.attach_head = hdi_output_attach_head;
    output->base.detach_head = hdi_output_detach_head;

    weston_compositor_add_pending_output(&output->base, compositor);
    return &output->base;
}
