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
#include <chrono>
#include <cinttypes>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <vector>

#include "hdi_backend.h"
#include "hdi_head.h"

// C header adapter
extern "C" {
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include "libweston/linux-dmabuf.h"
#include "shared/helpers.h"
}

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiRenderer");

struct hdi_renderer {
    struct weston_renderer base;
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
    LayerInfo layer_info;
    IRect dst_rect;
    IRect src_rect;
    uint32_t zorder;
    BlendType blend_type;
    CompositionType comp_type;
    TransformType rotate_type;
    BufferHandle *bh;
};

struct hdi_output_state {
    std::vector<struct hdi_surface_state *> layers;
    uint32_t gpu_layer_id;
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
        LOG_CORE("GrallocFuncs.Mmap fd=%d return ptr=%p", bh->fd, ptr);
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
        auto fd = bh->fd;
        auto ptr = bh->virAddr;
        auto ret = b->display_gralloc->Unmap(*bh);
        LOG_CORE("GrallocFuncs.Unmap fd=%d ptr=%p return %d", fd, ptr, ret);
    }
}

static void
hdi_renderer_surface_state_on_destroy(struct wl_listener *listener,
                                      void *data)
{
    LOG_PASS();
    struct hdi_surface_state *hss = container_of(listener,
                                                 struct hdi_surface_state,
                                                 surface_destroy_listener);
    struct hdi_backend *b = to_hdi_backend(hss->compositor);
    if (hss->layer_id != -1) {
        int ret = b->layer_funcs->CloseLayer(hss->device_id, hss->layer_id);
        LOG_CORE("LayerFuncs.CloseLayer lid=%d return %d", hss->layer_id, ret);
        hss->layer_id = -1;
    }

    hdi_renderer_surface_state_unmap(hss);
    weston_buffer_reference(&hss->buffer_ref, NULL);

    free(hss);
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

    surface->hdi_renderer_state = hss;
    hss->surface = surface;
    hss->compositor = surface->compositor;

    hss->surface_destroy_listener.notify =
        hdi_renderer_surface_state_on_destroy;
    wl_signal_add(&surface->destroy_signal,
        &hss->surface_destroy_listener);

    struct hdi_backend *b = to_hdi_backend(surface->compositor);

    // init
    hss->layer_id = -1;
    return 0;
}

static void
hdi_renderer_attach(struct weston_surface *surface,
                    struct weston_buffer *buffer)
{
    LOG_SCOPE();
    assert(surface && !"hdi_renderer_attach surface is NULL");
    assert(buffer && !"hdi_renderer_attach buffer is NULL");
    if (surface->hdi_renderer_state == NULL) {
        hdi_renderer_create_surface_state(surface);
    }

    struct hdi_surface_state *hss = (struct hdi_surface_state *)surface->hdi_renderer_state;
    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (dmabuf != NULL) {
        LOG_INFO("dmabuf");
        hdi_renderer_surface_state_unmap(hss);
        weston_buffer_reference(&hss->buffer_ref, buffer);
        buffer->width = dmabuf->attributes.width;
        buffer->height = dmabuf->attributes.height;
        return;
    }

    struct wl_shm_buffer *shmbuf = wl_shm_buffer_get(buffer->resource);
    if (shmbuf != NULL) {
        LOG_INFO("shmbuf");
        hdi_renderer_surface_state_unmap(hss);
        weston_buffer_reference(&hss->buffer_ref, buffer);
        buffer->width = wl_shm_buffer_get_width(shmbuf);
        buffer->height = wl_shm_buffer_get_height(shmbuf);
        return;
    }

    LOG_ERROR("cannot attach buffer");
}

static void
hdi_renderer_destroy(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_renderer *renderer = (struct hdi_renderer *)compositor->hdi_renderer;
    compositor->hdi_renderer = NULL;
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

    LOG_REGION(1, &surface_region);
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
    struct hdi_surface_state *hss = (struct hdi_surface_state *)view->surface->hdi_renderer_state;
    if (matrix.d[0] == matrix.d[5] && matrix.d[0] == 0) {
        if (matrix.d[4] > 0 && matrix.d[1] > 0) {
            LOG_INFO("Transform: 90 mirror");
            hss->rotate_type = ROTATE_90;
        } else if (matrix.d[4] < 0 && matrix.d[1] > 0) {
            LOG_INFO("Transform: 90");
            hss->rotate_type = ROTATE_90;
        } else if (matrix.d[4] < 0 && matrix.d[1] < 0) {
            LOG_INFO("Transform: 270 mirror");
            hss->rotate_type = ROTATE_270;
        } else if (matrix.d[4] > 0 && matrix.d[1] < 0) {
            LOG_INFO("Transform: 270");
            hss->rotate_type = ROTATE_270;
        }
    } else {
        if (matrix.d[0] > 0 && matrix.d[5] > 0) {
            LOG_INFO("Transform: 0");
            hss->rotate_type = ROTATE_NONE;
        } else if (matrix.d[0] < 0 && matrix.d[5] < 0) {
            LOG_INFO("Transform: 180");
            hss->rotate_type = ROTATE_180;
        } else if (matrix.d[0] < 0 && matrix.d[5] > 0) {
            LOG_INFO("Transform: 0 mirror");
            hss->rotate_type = ROTATE_NONE;
        } else if (matrix.d[0] > 0 && matrix.d[5] < 0) {
            LOG_INFO("Transform: 180 mirror");
            hss->rotate_type = ROTATE_180;
        }
    }

    LOG_MATRIX(&matrix);
    LOG_INFO("%d %d", view->surface->width, view->surface->height);

    weston_view_to_global_region(view, global_repaint_region, &surface_region);
    pixman_region32_intersect(global_repaint_region, global_repaint_region, &repaint_output);
    LOG_REGION(3, global_repaint_region);

    pixman_region32_t surface_repaint_region;
    pixman_region32_init(&surface_repaint_region);
    weston_view_from_global_region(view, &surface_repaint_region, global_repaint_region);
    LOG_REGION(4, &surface_repaint_region);

    pixman_region32_init(buffer_repaint_region);
    weston_surface_to_buffer_region(view->surface, &surface_repaint_region, buffer_repaint_region);
    LOG_REGION(5, buffer_repaint_region);
    pixman_region32_fini(&surface_repaint_region);
    pixman_region32_fini(&surface_region);
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
    if (hss->layer_id == -1) {
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
        if (ret != DISPLAY_SUCCESS) {
            LOG_ERROR("create layer failed");
            hss->layer_id = -1;
            return -1;
        }
        LOG_INFO("create layer: %d", hss->layer_id);
    } else {
        LOG_INFO("use layer: %d", hss->layer_id);
    }
    return 0;
}

static void
hdi_renderer_repaint_output(struct weston_output *output,
                            pixman_region32_t *output_damage)
{
    LOG_SCOPE();
    struct weston_compositor *compositor = output->compositor;
    struct hdi_backend *b = to_hdi_backend(compositor);
    struct weston_head *whead = weston_output_get_first_head(output);
    uint32_t device_id = hdi_head_get_device_id(whead);
    auto ho = reinterpret_cast<struct hdi_output_state *>(output->hdi_renderer_state);
    auto old_layers = ho->layers;
    ho->layers.clear();

    int32_t zorder = 2;
    struct weston_view *view;
    wl_list_for_each_reverse(view, &compositor->view_list, link) {
        if (view->renderer_type != WESTON_RENDERER_TYPE_HDI) {
            continue;
        }

        struct hdi_surface_state *hss = (struct hdi_surface_state *)view->surface->hdi_renderer_state;
        if (hss == NULL) {
            continue;
        }

        if (hdi_renderer_surface_state_create_layer(hss, b, output) != 0) {
            continue;
        }

        ho->layers.push_back(hss);
        hdi_renderer_surface_state_calc_rect(hss, output_damage, output, view);
        hss->zorder = zorder++;
        hss->blend_type = BLEND_SRCOVER;
        if (hss->surface->type == WL_SURFACE_TYPE_VIDEO) {
            hss->comp_type = COMPOSITION_VIDEO;
        } else {
            hss->comp_type = COMPOSITION_DEVICE;
            BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
        }
    }

    // close not composite layer
    for (auto &hss : old_layers) {
        bool occur = false;
        for (const auto &layer : ho->layers) {
            if (hss == layer) {
                occur = true;
                break;
            }
        }

        if (!occur) {
            int ret = b->layer_funcs->CloseLayer(hss->device_id, hss->layer_id);
            LOG_CORE("LayerFuncs.CloseLayer %d return %d", hss->layer_id, ret);
            hss->layer_id = -1;
        }
    }

    wl_list_for_each_reverse(view, &compositor->view_list, link) {
        if (view->renderer_type != WESTON_RENDERER_TYPE_HDI) {
            continue;
        }

        struct hdi_surface_state *hss = (struct hdi_surface_state *)view->surface->hdi_renderer_state;
        LOG_INFO("LayerOperation: %p", view);
        if (hss == NULL) {
            continue;
        }

        if (hdi_renderer_surface_state_create_layer(hss, b, output) != 0) {
            continue;
        }

        if (hss->surface->type != WL_SURFACE_TYPE_VIDEO) {
            BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
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
        ret = b->layer_funcs->SetTransformMode(device_id, hss->layer_id, hss->rotate_type);
        LOG_CORE("LayerFuncs.SetTransformMode return %d", ret);
    }
}

static void
hdi_renderer_surface_set_color(struct weston_surface *surface,
                               float red, float green,
                               float blue, float alpha)
{
}

static void
hdi_renderer_surface_get_content_size(struct weston_surface *surface,
                               int *width, int *height)
{
    struct hdi_surface_state *hss = (struct hdi_surface_state *)surface->hdi_renderer_state;
    if (hss == NULL) {
        LOG_ERROR("hdi_renderer_state is null\n");
        *width = 0;
        *height = 0;
        return;
    }
    BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
    if (bh == NULL) {
        LOG_ERROR("hdi_renderer_surface_state_mmap error\n");
        *width = 0;
        *height = 0;
        return;
    }

    *width = bh->width;
    *height = bh->height;
    return;
}

static int
hdi_renderer_surface_copy_content(struct weston_surface *surface,
                                  void *target, size_t size,
                                  int src_x, int src_y, int width, int height)
{
    struct hdi_surface_state *hss = (struct hdi_surface_state *)surface->hdi_renderer_state;
    if (hss == NULL) {
        LOG_ERROR("hdi_renderer_state is null\n");
        return -1;
    }


    BufferHandle *bh = hdi_renderer_surface_state_mmap(hss);
    if (bh == NULL) {
        LOG_ERROR("hdi_renderer_surface_state_mmap error\n");
        return -1;
    }

    memcpy(target, bh->virAddr, size);
    return 0;
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
    renderer->base.surface_copy_content = hdi_renderer_surface_copy_content;
    renderer->base.surface_get_content_size = hdi_renderer_surface_get_content_size;

    compositor->hdi_renderer = &renderer->base;
    return 0;
}

int
hdi_renderer_output_create(struct weston_output *output,
    const struct hdi_renderer_output_options *options)
{
    LOG_SCOPE();
    auto ho = new struct hdi_output_state();
    ho->gpu_layer_id = -1;
    output->hdi_renderer_state = ho;
    return 0;
}

void
hdi_renderer_output_destroy(struct weston_output *output)
{
    LOG_SCOPE();
    auto ho = (struct hdi_output_state *)output->hdi_renderer_state;
    if (ho->gpu_layer_id == DISPLAY_SUCCESS) {
        struct hdi_backend *b = to_hdi_backend(output->compositor);
        struct weston_head *whead = weston_output_get_first_head(output);
        uint32_t device_id = hdi_head_get_device_id(whead);
        int ret = b->layer_funcs->CloseLayer(device_id, ho->gpu_layer_id);
        LOG_CORE("LayerFuncs.CloseLayer return %d", ret);
    }

    delete ho;
}

void
hdi_renderer_output_set_gpu_buffer(struct weston_output *output, BufferHandle *buffer)
{
    LOG_SCOPE();
    struct hdi_backend *b = to_hdi_backend(output->compositor);
    struct hdi_output_state *ho =
        (struct hdi_output_state *)output->hdi_renderer_state;
    struct weston_head *whead = weston_output_get_first_head(output);
    int32_t device_id = hdi_head_get_device_id(whead);

    // close last gpu layer
    if (ho->gpu_layer_id != -1) {
        int ret = b->layer_funcs->CloseLayer(device_id, ho->gpu_layer_id);
        LOG_CORE("LayerFuncs.CloseLayer return %d", ret);
    }

    // create layer
    LayerInfo layer_info = {
        .width = buffer->width,
        .height = buffer->height,
        .type = LAYER_TYPE_GRAPHIC,
        .bpp = buffer->stride * 0x8 / buffer->width,
        .pixFormat = (PixelFormat)buffer->format,
    };
    int ret = b->layer_funcs->CreateLayer(device_id, &layer_info, &ho->gpu_layer_id);
    LOG_CORE("LayerFuncs.CreateLayer return %d", ret);
    if (ret != DISPLAY_SUCCESS) {
        LOG_ERROR("create layer failed");
        ho->gpu_layer_id = -1;
        return;
    }
    LOG_INFO("create layer %d", ho->gpu_layer_id);

    // param
    LayerAlpha alpha = { .enPixelAlpha = true };
    int32_t fence = -1;
    IRect dst_rect = { .w = buffer->width, .h = buffer->height, };
    IRect src_rect = dst_rect;
    int32_t zorder = 1; // 1 for gpu
    BlendType blend_type = BLEND_SRC;
    CompositionType comp_type = COMPOSITION_DEVICE;

    // layer operation
    ret = b->layer_funcs->SetLayerAlpha(device_id, ho->gpu_layer_id, &alpha);
    LOG_CORE("LayerFuncs.SetLayerAlpha return %d", ret);
    ret = b->layer_funcs->SetLayerBuffer(device_id, ho->gpu_layer_id, buffer, fence);
    LOG_CORE("LayerFuncs.SetLayerBuffer return %d", ret);
    ret = b->layer_funcs->SetLayerSize(device_id, ho->gpu_layer_id, &dst_rect);
    LOG_CORE("LayerFuncs.SetLayerSize return %d", ret);
    ret = b->layer_funcs->SetLayerCrop(device_id, ho->gpu_layer_id, &src_rect);
    LOG_CORE("LayerFuncs.SetLayerCrop return %d", ret);
    ret = b->layer_funcs->SetLayerZorder(device_id, ho->gpu_layer_id, zorder);
    LOG_CORE("LayerFuncs.SetLayerZorder return %d", ret);
    ret = b->layer_funcs->SetLayerBlendType(device_id, ho->gpu_layer_id, blend_type);
    LOG_CORE("LayerFuncs.SetLayerBlendType return %d", ret);
    ret = b->layer_funcs->SetLayerCompositionType(device_id, ho->gpu_layer_id, comp_type);
    LOG_CORE("LayerFuncs.SetLayerCompositionType return %d", ret);
}
