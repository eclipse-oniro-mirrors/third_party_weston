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

#include "hdi_renderer.h"

#include <assert.h>
#include <cinttypes>
#include <sstream>
#include <sys/time.h>

// C header adapter
extern "C" {
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include "libweston/linux-dmabuf.h"
#include "shared/helpers.h"
}

#include "hdi_backend.h"
#include "hdi_head.h"

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiRenderr");

struct hdi_renderer {
    struct weston_renderer base;
};

struct hdi_output_state {
    int a;
};

struct hdi_surface_state {
    // basic attribute
    struct weston_compositor *compositor;
    struct weston_surface *surface;
    struct wl_listener surface_destroy_listener;
    struct weston_buffer_reference buffer_ref;

    // hdi attribute
    uint32_t device_id;
    uint32_t layer_id;
    int32_t create_layer_retval;
    LayerInfo layer_info;
    IRect dst_rect;
    IRect src_rect;
    uint32_t zorder;
    BlendType blend_type;
    CompositionType comp_type;
    BufferHandle *bh;
};

static BufferHandle *
hdi_renderer_surface_state_mmap(struct hdi_surface_state *hss)
{
    if (hss == NULL || hss->surface == NULL) {
        return NULL;
    }

    struct weston_buffer *buffer = hss->buffer_ref.buffer;
    if (buffer == NULL) {
        return NULL;
    }

    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (dmabuf == NULL) {
        return NULL;
    }

    BufferHandle *bh = dmabuf->attributes.buffer_handle;
    if (bh == NULL) {
        return NULL;
    }

    if (bh->virAddr == NULL) {
        struct hdi_backend *b = to_hdi_backend(hss->surface->compositor);
        void *ptr = b->display_gralloc->Mmap(*bh);
        LOG_CORE("GrallocFuncs.Mmap %d return %p", bh->fd, ptr);
    }
    return bh;
}

static void
hdi_renderer_surface_state_unmap(struct hdi_surface_state *hss)
{
    if (hss == NULL || hss->surface == NULL) {
        return;
    }

    struct weston_buffer *buffer = hss->buffer_ref.buffer;
    if (buffer == NULL) {
        return;
    }

    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (dmabuf == NULL) {
        return;
    }

    BufferHandle *bh = dmabuf->attributes.buffer_handle;
    if (bh == NULL) {
        return;
    }

    if (bh->virAddr != NULL) {
        struct hdi_backend *b = to_hdi_backend(hss->compositor);
        int ret = b->display_gralloc->Unmap(*bh);
        LOG_CORE("GrallocFuncs.Mmap return %d", ret);
    }
}

static void
hdi_renderer_surface_state_destroy(struct hdi_surface_state *hss)
{
    LOG_PASS();
    struct hdi_backend *b = to_hdi_backend(hss->compositor);
    if (hss->create_layer_retval == DISPLAY_SUCCESS) {
        int ret = b->layer_funcs->CloseLayer(hss->device_id, hss->layer_id);
        LOG_CORE("LayerFuncs.CloseLayer return %d", ret);
    }

    hdi_renderer_surface_state_unmap(hss);
    weston_buffer_reference(&hss->buffer_ref, NULL);

    free(hss);
}

static void
hdi_renderer_surface_state_on_destroy(struct wl_listener *listener,
                                      void *data)
{
    LOG_PASS();
    struct hdi_surface_state *hss = container_of(listener,
                                                 struct hdi_surface_state,
                                                 surface_destroy_listener);
    hdi_renderer_surface_state_destroy(hss);
}

static int
hdi_renderer_create_surface_state(struct weston_surface *surface)
{
    LOG_PASS();
    // life time
    struct hdi_surface_state *hss = (struct hdi_surface_state *)zalloc(sizeof *hss);
    if (hss == NULL) {
        return -1;
    }

    surface->renderer_state = hss;
    hss->surface = surface;
    hss->compositor = surface->compositor;

    hss->surface_destroy_listener.notify =
        hdi_renderer_surface_state_on_destroy;
    wl_signal_add(&surface->destroy_signal,
        &hss->surface_destroy_listener);

    struct hdi_backend *b = to_hdi_backend(surface->compositor);

    // init
    hss->create_layer_retval = -1;
    return 0;
}

static void
hdi_renderer_attach(struct weston_surface *surface,
                    struct weston_buffer *buffer)
{
    LOG_PASS();
    assert(surface && "hdi_renderer_attach surface is NULL");
    assert(buffer && "hdi_renderer_attach buffer is NULL");
    if (surface->renderer_state == NULL) {
        hdi_renderer_create_surface_state(surface);
    }

    struct hdi_surface_state *hss = (struct hdi_surface_state *)surface->renderer_state;
    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (dmabuf != NULL) {
        weston_log("hdi_renderer_attach dmabuf");
        hdi_renderer_surface_state_unmap(hss);
        weston_buffer_reference(&hss->buffer_ref, buffer);
        buffer->width = dmabuf->attributes.width;
        buffer->height = dmabuf->attributes.height;
        return;
    }

    struct wl_shm_buffer *shmbuf = wl_shm_buffer_get(buffer->resource);
    if (shmbuf != NULL) {
        weston_log("hdi_renderer_attach shmbuf");
        hdi_renderer_surface_state_unmap(hss);
        weston_buffer_reference(&hss->buffer_ref, buffer);
        buffer->width = wl_shm_buffer_get_width(shmbuf);
        buffer->height = wl_shm_buffer_get_height(shmbuf);
        return;
    }

    weston_log("hdi_renderer_attach cannot attach buffer");
}

static void
hdi_renderer_destroy(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_renderer *renderer = (struct hdi_renderer *)compositor->renderer;
    compositor->renderer = NULL;
    free(renderer);
}

static void
hdi_renderer_flush_damage(struct weston_surface *surface)
{
}

static bool
hdi_renderer_import_dmabuf(struct weston_compositor *compositor,
                           struct linux_dmabuf_buffer *buffer)
{
    return true;
}

static void
hdi_renderer_query_dmabuf_formats(struct weston_compositor *compositor,
                                  int **formats, int *num_formats)
{
    *num_formats = 0;
    *formats = NULL;
}

static void
hdi_renderer_query_dmabuf_modifiers(struct weston_compositor *compositorc,
                                    int format,
                                    uint64_t **modifiers,
                                    int *num_modifiers)
{
    *num_modifiers = 0;
    *modifiers = NULL;
}

static int
hdi_renderer_read_pixels(struct weston_output *output,
            pixman_format_code_t format, void *pixels,
            uint32_t x, uint32_t y,
            uint32_t width, uint32_t height)
{
    return 0;
}

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

typedef void(*weston_view_compute_global_region_func)(struct weston_view *view,
    float x, float y, float *vx, float *vy);

static void
weston_view_compute_global_region(struct weston_view *view,
                                  pixman_region32_t *outr,
                                  pixman_region32_t *inr,
                                  weston_view_compute_global_region_func fn)
{
    float min_x = HUGE_VALF,  min_y = HUGE_VALF;
    float max_x = -HUGE_VALF, max_y = -HUGE_VALF;
    pixman_box32_t *inbox = pixman_region32_extents(inr);
    int32_t vs[4][2] = {
        { inbox->x1, inbox->y1 },
        { inbox->x1, inbox->y2 },
        { inbox->x2, inbox->y1 },
        { inbox->x2, inbox->y2 },
    };

    if (inbox->x1 == inbox->x2 || inbox->y1 == inbox->y2) {
        pixman_region32_init(outr);
        return;
    }

    for (int i = 0; i < 4; i++) {
        float x, y;
        fn(view, vs[i][0], vs[i][1], &x, &y);
        min_x = min(min_x, x);
        max_x = max(max_x, x);
        min_y = min(min_y, y);
        max_y = max(max_y, y);
    }

    float int_x = floorf(min_x);
    float int_y = floorf(min_y);
    pixman_region32_init_rect(outr, int_x, int_y,
            ceilf(max_x) - int_x, ceilf(max_y) - int_y);
}

#undef min
#undef max

static void
weston_view_to_global_region(struct weston_view *view,
                             pixman_region32_t *outr,
                             pixman_region32_t *inr)
{
    weston_view_compute_global_region(view, outr, inr, weston_view_to_global_float);
}

static void
weston_view_from_global_region(struct weston_view *view,
                               pixman_region32_t *outr,
                               pixman_region32_t *inr)
{
    weston_view_compute_global_region(view, outr, inr, weston_view_from_global_float);
}

static void
hdi_renderer_repaint_output_calc_region(pixman_region32_t *global_repaint_region,
                                        pixman_region32_t *buffer_repaint_region,
                                        pixman_region32_t *output_damage,
                                        struct weston_output *output,
                                        struct weston_view *view)
{
    pixman_region32_t surface_region;
    pixman_region32_init_rect(&surface_region, 0, 0, view->surface->width, view->surface->height);

    pixman_region32_t buffer_region;
    pixman_region32_init(&buffer_region);
    weston_surface_to_buffer_region(view->surface, &surface_region, &buffer_region);
    pixman_region32_fini(&surface_region);

    pixman_region32_t repaint_output;
    pixman_region32_init(&repaint_output);
    pixman_region32_copy(&repaint_output, output_damage);
    if (output->zoom.active) {
        weston_matrix_transform_region(&repaint_output, &output->matrix, &repaint_output);
    } else {
        pixman_region32_translate(&repaint_output, -output->x, -output->y);
        weston_transformed_region(output->width, output->height,
                static_cast<enum wl_output_transform>(output->transform),
                output->current_scale,
                &repaint_output, &repaint_output);
    }

    LOG_REGION(1, &buffer_region);
    LOG_REGION(2, &repaint_output);

    struct weston_matrix matrix = output->inverse_matrix;
    if (view->transform.enabled) {
        weston_matrix_multiply(&matrix, &view->transform.inverse);
        LOG_INFO("transform enabled");
    } else {
        weston_matrix_translate(&matrix,
                -view->geometry.x, -view->geometry.y, 0);
        LOG_INFO("transform disabled");
    }
    weston_matrix_multiply(&matrix, &view->surface->surface_to_buffer_matrix);

    LOG_INFO("%f %f %f %f", matrix.d[0], matrix.d[1], matrix.d[2], matrix.d[3]);
    LOG_INFO("%f %f %f %f", matrix.d[4], matrix.d[5], matrix.d[6], matrix.d[7]);
    LOG_INFO("%f %f %f %f", matrix.d[8], matrix.d[9], matrix.d[10], matrix.d[11]);
    LOG_INFO("%f %f %f %f", matrix.d[12], matrix.d[13], matrix.d[14], matrix.d[15]);
    LOG_INFO("%d %d", view->surface->width, view->surface->height);

    weston_view_to_global_region(view, global_repaint_region, &buffer_region);
    pixman_region32_intersect(global_repaint_region, global_repaint_region, &repaint_output);
    LOG_REGION(3, global_repaint_region);

    weston_view_from_global_region(view, buffer_repaint_region, global_repaint_region);
    LOG_REGION(4, buffer_repaint_region);

    pixman_region32_fini(&buffer_region);
    pixman_region32_fini(&repaint_output);
}

static void
hdi_renderer_surface_state_calc_rect(struct hdi_surface_state *hss,
    pixman_region32_t *output_damage, struct weston_output *output, struct weston_view *view)
{
    pixman_region32_t global_repaint_region;
    pixman_region32_t buffer_repaint_region;
    hdi_renderer_repaint_output_calc_region(&global_repaint_region,
                                            &buffer_repaint_region,
                                            output_damage,
                                            output, view);

    pixman_box32_t *global_box = pixman_region32_extents(&global_repaint_region);
    hss->dst_rect.x = global_box->x1;
    hss->dst_rect.y = global_box->y1;
    hss->dst_rect.w = global_box->x2 - global_box->x1;
    hss->dst_rect.h = global_box->y2 - global_box->y1;

    pixman_box32_t *buffer_box = pixman_region32_extents(&buffer_repaint_region);
    hss->src_rect.x = buffer_box->x1;
    hss->src_rect.y = buffer_box->y1;
    hss->src_rect.w = buffer_box->x2 - buffer_box->x1;
    hss->src_rect.h = buffer_box->y2 - buffer_box->y1;

    pixman_region32_fini(&global_repaint_region);
    pixman_region32_fini(&buffer_repaint_region);
}

static int
hdi_renderer_surface_state_create_layer(struct hdi_surface_state *hss,
    struct hdi_backend *b, struct weston_output *output)
{
    struct weston_mode *mode = output->current_mode;
    if (hss->create_layer_retval != DISPLAY_SUCCESS) {
        hss->layer_info.width = mode->width;
        hss->layer_info.height = mode->height;
        if (hss->surface->type == WL_SURFACE_TYPE_VIDEO) {
            // video
        } else {
            // other
            BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
            hss->layer_info.bpp = bh->stride * 0x8 / bh->width;
            hss->layer_info.pixFormat = (PixelFormat)bh->format;
            hss->bh = bh;
        }
        hss->layer_info.type = LAYER_TYPE_GRAPHIC;
        struct weston_head *whead = weston_output_get_first_head(output);
        hss->device_id = hdi_head_get_device_id(whead);
        int ret = b->layer_funcs->CreateLayer(hss->device_id,
                                              &hss->layer_info, &hss->layer_id);
        LOG_CORE("LayerFuncs.CreateLayer return %d", ret);
        hss->create_layer_retval = ret;
        if (ret != DISPLAY_SUCCESS) {
            weston_log("layer create failed");
            LOG_ERROR("create layer failed");
            return -1;
        }
        LOG_INFO("create layer: %d", hss->layer_id);
    } else {
        LOG_INFO("use layer: %d", hss->layer_id);
    }
    return 0;
}

static void dump_to_file(BufferHandle *bh)
{
    if (bh == NULL) {
        return;
    }

    if (access("/data/hdi_dump", F_OK) == -1) {
        return;
    }

    struct timeval now;
    gettimeofday(&now, nullptr);
    constexpr int secToUsec = 1000 * 1000;
    int64_t nowVal = (int64_t)now.tv_sec * secToUsec + (int64_t)now.tv_usec;

    std::stringstream ss;
    ss << "/data/hdi-dumpimage-" << nowVal << ".raw";
    weston_log("dumpimage: %{public}s", ss.str().c_str());
    weston_log("fd: %{public}d", bh->fd);
    weston_log("width: %{public}d", bh->width);
    weston_log("height: %{public}d", bh->height);
    weston_log("size: %{public}d", bh->size);
    weston_log("format: %{public}d", bh->format);
    weston_log("usage: %{public}" PRIu64, bh->usage);
    weston_log("virAddr: %{public}p", bh->virAddr);
    weston_log("phyAddr: %{public}" PRIu64, bh->phyAddr);

    auto fp = fopen(ss.str().c_str(), "a+");
    if (fp == nullptr) {
        return;
    }

    fwrite(bh->virAddr, bh->size, 1, fp);
    fclose(fp);
}

static void
hdi_renderer_repaint_output(struct weston_output *output,
                            pixman_region32_t *output_damage)
{
    LOG_ENTER();
    struct weston_compositor *compositor = output->compositor;
    struct hdi_backend *b = to_hdi_backend(compositor);
    struct weston_head *whead = weston_output_get_first_head(output);
    uint32_t device_id = hdi_head_get_device_id(whead);

    int32_t zorder = 1;
    BlendType blend_type = BLEND_SRC;
    struct weston_view *view;
    wl_list_for_each_reverse(view, &compositor->view_list, link) {
        struct hdi_surface_state *hss = (struct hdi_surface_state *)view->surface->renderer_state;
        if (hss == NULL) {
            continue;
        }

        if (hdi_renderer_surface_state_create_layer(hss, b, output) != 0) {
            continue;
        }

        hdi_renderer_surface_state_calc_rect(hss, output_damage, output, view);
        hss->zorder = zorder++;
        hss->blend_type = blend_type;
        blend_type = BLEND_SRCOVER;
        if (hss->surface->type == WL_SURFACE_TYPE_VIDEO) {
            hss->comp_type = COMPOSITION_VIDEO;
        } else {
            hss->comp_type = COMPOSITION_DEVICE;
            BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
            dump_to_file(bh);
            int ret = b->layer_funcs->SetLayerBuffer(device_id, hss->layer_id, bh, -1);
            LOG_CORE("LayerFuncs.SetLayerBuffer return %d", ret);
        }

        LayerAlpha alpha = { .enPixelAlpha = true };
        int ret = b->layer_funcs->SetLayerAlpha(device_id, hss->layer_id, &alpha);
        LOG_CORE("LayerFuncs.SetLayerAlpha return %d", ret);
        ret = b->layer_funcs->SetLayerSize(device_id, hss->layer_id, &hss->dst_rect);
        LOG_CORE("LayerFuncs.SetLayerSize return %d", ret);
        ret = b->layer_funcs->SetLayerCrop(device_id, hss->layer_id, &hss->src_rect);
        LOG_CORE("LayerFuncs.SetLayerCrop return %d", ret);
        ret = b->layer_funcs->SetLayerZorder(device_id, hss->layer_id, hss->zorder);
        LOG_CORE("LayerFuncs.SetLayerZorder return %d", ret);
        ret = b->layer_funcs->SetLayerBlendType(device_id, hss->layer_id, hss->blend_type);
        LOG_CORE("LayerFuncs.SetLayerBlendType return %d", ret);
        ret = b->layer_funcs->SetLayerCompositionType(device_id, hss->layer_id, hss->comp_type);
        LOG_CORE("LayerFuncs.SetLayerCompositionType return %d", ret);
        b->layer_funcs->SetTransformMode(device_id, hss->layer_id, ROTATE_NONE);
    }
    LOG_EXIT();
}

static void
hdi_renderer_surface_set_color(struct weston_surface *surface,
                               float red, float green,
                               float blue, float alpha)
{
}

int
hdi_renderer_init(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_renderer *renderer = (struct hdi_renderer *)zalloc(sizeof *renderer);

    renderer->base.attach = hdi_renderer_attach;
    renderer->base.destroy = hdi_renderer_destroy;
    renderer->base.flush_damage = hdi_renderer_flush_damage;
    renderer->base.import_dmabuf = hdi_renderer_import_dmabuf;
    renderer->base.query_dmabuf_formats = hdi_renderer_query_dmabuf_formats;
    renderer->base.query_dmabuf_modifiers = hdi_renderer_query_dmabuf_modifiers;
    renderer->base.read_pixels = hdi_renderer_read_pixels;
    renderer->base.repaint_output = hdi_renderer_repaint_output;
    renderer->base.surface_set_color = hdi_renderer_surface_set_color;
    renderer->base.surface_copy_content = NULL;
    renderer->base.surface_get_content_size = NULL;

    compositor->renderer = &renderer->base;
    return 0;
}

int
hdi_renderer_output_create(struct weston_output *output,
    const struct hdi_renderer_output_options *options)
{
    LOG_PASS();
    struct hdi_output_state *ho = (struct hdi_output_state *)zalloc(sizeof *ho);
    output->renderer_state = ho;
    return 0;
}

void
hdi_renderer_output_destroy(struct weston_output *output)
{
    LOG_PASS();
    struct hdi_output_state *ho =
        (struct hdi_output_state *)output->renderer_state;
    free(ho);
}
