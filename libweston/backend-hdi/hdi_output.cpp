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
    BufferHandle *framebuffer[HDI_OUTPUT_FRMAEBUFFER_SIZE];
    BufferHandle *gl_render_framebuffer[HDI_OUTPUT_FRMAEBUFFER_GL_SIZE];
    uint32_t current_framebuffer_id;
    struct wl_event_source *finish_frame_timer;
};

static struct hdi_output *
to_hdi_output(struct weston_output *base)
{
    return container_of(base, struct hdi_output, base);
}

static int
hdi_output_start_repaint_loop(struct weston_output *output)
{
    LOG_ENTER();
    struct timespec ts;
    weston_compositor_read_presentation_clock(output->compositor, &ts);
    weston_output_finish_frame(output, &ts, WP_PRESENTATION_FEEDBACK_INVALID);
    LOG_EXIT();
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

static int
hdi_output_repaint(struct weston_output *output_base,
                   pixman_region32_t *damage,
                   void *repaint_data)
{
    LOG_ENTER();
    struct hdi_pending_state *hps =
        reinterpret_cast<struct hdi_pending_state *>(repaint_data);
    struct hdi_output *output = to_hdi_output(output_base);
    struct weston_head *h = weston_output_get_first_head(output_base);
    hps->device_id = hdi_head_get_device_id(h);

    // prepare framebuffer
    output->current_framebuffer_id = (output->current_framebuffer_id + 1) % 2;
    auto &hdi_framebuffer = output->framebuffer[output->current_framebuffer_id];
    auto &gl_framebuffer = output->gl_render_framebuffer[output->current_framebuffer_id];

    // assign view to renderer
    bool need_gpu_render = false;
    bool need_hdi_render = false;
    struct weston_view *view;
    wl_list_for_each_reverse(view, &output_base->compositor->view_list, link) {
        if (view->renderer_type == WESTON_RENDERER_TYPE_GPU) {
            need_gpu_render = true;
        } else if (view->renderer_type == WESTON_RENDERER_TYPE_HDI) {
            need_hdi_render = true;
        }
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
        hps->framebuffer = hdi_framebuffer;
    } else {
        hps->framebuffer = gl_framebuffer;
    }

    hdi_output_active_timer(output);
    LOG_EXIT();
    return 0;
}

int
hdi_output_set_mode(struct weston_output *base)
{
    LOG_ENTER();
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

    LOG_EXIT();
    return 0;
}

static int
hdi_output_enable(struct weston_output *base)
{
    LOG_ENTER();
    struct hdi_output *output = to_hdi_output(base);
    struct hdi_backend *b = to_hdi_backend(base->compositor);
    struct gl_renderer_fbo_options fbo_options;

    switch (b->renderer_type) {
        case HDI_RENDERER_HDI:
            break;
    }

    AllocInfo info = {
        .width = output->mode.width,
        .height = output->mode.height,
        .usage = HBM_USE_MEM_DMA | HBM_USE_CPU_READ | HBM_USE_CPU_WRITE,
        .format = PIXEL_FMT_BGRA_8888,
    };
    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_SIZE; i++) {
        int ret = b->display_gralloc->AllocMem(info, output->framebuffer[i]);
        LOG_CORE("GrallocFuncs.AllocMem return %d", ret);
        void *ptr = b->display_gralloc->Mmap(*output->framebuffer[i]);
        LOG_CORE("GrallocFuncs.Mmap return %p", output->framebuffer[i]->virAddr);
    }

    info.format = PIXEL_FMT_RGBA_8888;
    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_GL_SIZE; i++) {
        int ret = b->display_gralloc->AllocMem(info, output->gl_render_framebuffer[i]);
        LOG_CORE("GrallocFuncs.AllocMem return %d", ret);
        fbo_options.handle[i] = output->gl_render_framebuffer[i];
        void *ptr = b->display_gralloc->Mmap(*output->gl_render_framebuffer[i]);
        LOG_CORE("GrallocFuncs.Mmap return %p", output->gl_render_framebuffer[i]->virAddr);
    }

    if (base->compositor->gpu_renderer) {
        b->glri->output_fbo_create(base, &fbo_options);
    }
    output->base.start_repaint_loop = hdi_output_start_repaint_loop;
    output->base.repaint = hdi_output_repaint;
    output->base.assign_planes = NULL;
    output->base.set_dpms = NULL;
    output->base.switch_mode = NULL;
    output->base.set_gamma = NULL;
    output->base.set_backlight = NULL;
    output->current_framebuffer_id = 0;
    hdi_renderer_output_create(base, NULL);
    hdi_output_create_timer(output);
    hdi_output_active_timer(output);

    LOG_EXIT();
    return 0;
}

static int
hdi_output_disable(struct weston_output *base)
{
    LOG_ENTER();
    struct hdi_output *output = to_hdi_output(base);
    struct hdi_backend *b = to_hdi_backend(base->compositor);

    if (!base->enabled) {
        LOG_EXIT();
        return 0;
    }

    hdi_output_destroy_timer(output);
    hdi_renderer_output_destroy(base);

    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_SIZE; i++) {
        int ret = b->display_gralloc->Unmap(*output->framebuffer[i]);
        LOG_CORE("GrallocFuncs.Unmap");
        b->display_gralloc->FreeMem(*output->framebuffer[i]);
        LOG_CORE("GrallocFuncs.FreeMem");
    }

    for (int i = 0; i < HDI_OUTPUT_FRMAEBUFFER_GL_SIZE; i++) {
        int ret = b->display_gralloc->Unmap(*output->gl_render_framebuffer[i]);
        LOG_CORE("GrallocFuncs.Unmap");
        b->display_gralloc->FreeMem(*output->gl_render_framebuffer[i]);
        LOG_CORE("GrallocFuncs.FreeMem");
    }


    switch (b->renderer_type) {
        case HDI_RENDERER_HDI:
            break;
    }

    LOG_EXIT();
    return 0;
}

static void
hdi_output_destroy(struct weston_output *base)
{
    LOG_ENTER();
    hdi_output_disable(base);
    weston_output_release(base);

    free(to_hdi_output(base));
    LOG_EXIT();
}

static int
hdi_output_attach_head(struct weston_output *output_base,
                       struct weston_head *head_base)
{
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
    if (output_base->enabled == false) {
        return;
    }

    weston_output_schedule_repaint(output_base);
}

struct weston_output *
hdi_output_create(struct weston_compositor *compositor, const char *name)
{
    LOG_ENTER();

    assert(name && "name cannot be NULL.");

    struct hdi_output *output = (struct hdi_output *)zalloc(sizeof *output);
    if (!output) {
        weston_log("zalloc hdi_output failed");
        LOG_EXIT();
        return NULL;
    }

    weston_output_init(&output->base, compositor, name);

    output->base.enable = hdi_output_enable;
    output->base.destroy = hdi_output_destroy;
    output->base.disable = hdi_output_disable;
    output->base.attach_head = hdi_output_attach_head;
    output->base.detach_head = hdi_output_detach_head;

    weston_compositor_add_pending_output(&output->base, compositor);

    LOG_EXIT();
    return &output->base;
}
