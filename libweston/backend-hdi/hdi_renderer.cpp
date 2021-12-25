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
#include <map>
#include <set>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <vector>

#include "hdi_backend.h"
#include "hdi_head.h"
#include "hdi_output.h"

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

struct hdi_output_state;
struct hdi_surface_state {
    // basic attribute
    struct weston_compositor *compositor;
    struct weston_surface *surface;
    struct wl_listener surface_destroy_listener;
    struct weston_buffer_reference buffer_ref;

    // hdi cache attribute
    std::map<uint32_t, uint32_t> layer_ids; // device_id: layer_id
    std::map<uint32_t, struct hdi_output_state *> hos; // device_id: ho

    // hdi once attribute
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
    std::set<struct hdi_surface_state *> layers;
    uint32_t gpu_layer_id;
};

struct hdi_output_state * get_output_state(struct weston_output *output)
{
    return reinterpret_cast<struct hdi_output_state *>(output->hdi_renderer_state);
}

struct hdi_surface_state * get_surface_state(struct weston_surface *surface)
{
    if (surface->hdi_renderer_state == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<struct hdi_surface_state *>(surface->hdi_renderer_state);
}

void hdi_renderer_layer_operation(struct hdi_backend *b, int32_t device_id, int32_t layer_id,
                             BufferHandle *buffer, int32_t fence,
                             LayerAlpha *alpha,
                             IRect *dst,
                             IRect *src,
                             uint32_t zorder,
                             BlendType blend_type,
                             CompositionType comp_type,
                             TransformType rotate_type)
{
    LayerDumpInfo dump_info = {
        .alpha = *alpha,
        .src = *src,
        .dst = *dst,
        .zorder = zorder,
        .blend_type = blend_type,
        .comp_type = comp_type,
        .rotate_type = rotate_type,
    };
    b->layer_dump_info_pending[device_id][layer_id] = dump_info;

    LOG_CORE("LayerOperation device_id=%d layer_id=%d", device_id, layer_id);
    if (buffer != nullptr) {
        auto ret = b->layer_funcs->SetLayerBuffer(device_id, layer_id, buffer, fence);
        LOG_CORE("LayerFuncs.SetLayerBuffer return %d", ret);
    }

    auto ret = b->layer_funcs->SetLayerAlpha(device_id, layer_id, alpha);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerAlpha", ret);

    ret = b->layer_funcs->SetLayerSize(device_id, layer_id, dst);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerSize (%d, %d) %dx%d", ret, dst->x, dst->y, dst->w, dst->h);

    ret = b->layer_funcs->SetLayerCrop(device_id, layer_id, src);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerCrop (%d, %d) %dx%d", ret, src->x, src->y, src->w, src->h);

    ret = b->layer_funcs->SetLayerZorder(device_id, layer_id, zorder);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerZorder %d", ret, zorder);

    ret = b->layer_funcs->SetLayerBlendType(device_id, layer_id, blend_type);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerBlendType %d", ret, blend_type);

    ret = b->layer_funcs->SetLayerCompositionType(device_id, layer_id, comp_type);
    LOG_CORE("[ret=%d] LayerFuncs.SetLayerCompositionType %d", ret, comp_type);

    ret = b->layer_funcs->SetTransformMode(device_id, layer_id, rotate_type);
    LOG_CORE("[ret=%d] LayerFuncs.SetTransformMode %d", ret, rotate_type);
}

void hdi_renderer_layer_close(struct hdi_backend *b, int32_t device_id, int32_t layer_id)
{
    int ret = b->layer_funcs->CloseLayer(device_id, layer_id);
    LOG_CORE("[ret=%d] LayerFuncs.CloseLayer device_id: %d, layer_id: %d", ret, device_id, layer_id);
}

BufferHandle * hdi_renderer_surface_state_mmap(struct hdi_surface_state *hss)
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

void hdi_renderer_surface_state_unmap(struct hdi_surface_state *hss)
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

void hdi_renderer_surface_state_on_destroy(struct wl_listener *listener,
                                      void *data)
{
    LOG_PASS();
    struct hdi_surface_state *hss = container_of(listener,
                                                 struct hdi_surface_state,
                                                 surface_destroy_listener);
    struct hdi_backend *b = to_hdi_backend(hss->compositor);
    for (const auto &[device_id, layer_id] : hss->layer_ids) {
        hdi_renderer_layer_close(b, device_id, layer_id);

        // delete old layers in ho's cache
        auto it = hss->hos.find(device_id);
        if (it != hss->hos.end()) {
            it->second->layers.erase(hss);
        }
    }

    hdi_renderer_surface_state_unmap(hss);
    weston_buffer_reference(&hss->buffer_ref, NULL);

    free(hss);
}

int hdi_renderer_create_surface_state(struct weston_surface *surface)
{
    LOG_PASS();
    // life time
    auto hss = new struct hdi_surface_state();
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
    return 0;
}

void hdi_renderer_attach(struct weston_surface *surface,
                    struct weston_buffer *buffer)
{
    LOG_SCOPE();
    assert(surface && !"hdi_renderer_attach surface is NULL");
    assert(buffer && !"hdi_renderer_attach buffer is NULL");
    if (surface->hdi_renderer_state == NULL) {
        hdi_renderer_create_surface_state(surface);
    }

    auto hss = get_surface_state(surface);
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

void hdi_renderer_destroy(struct weston_compositor *compositor)
{
    LOG_PASS();
    struct hdi_renderer *renderer = (struct hdi_renderer *)compositor->hdi_renderer;
    compositor->hdi_renderer = NULL;
    free(renderer);
}

void hdi_renderer_flush_damage(struct weston_surface *surface)
{
}

bool hdi_renderer_import_dmabuf(struct weston_compositor *compositor,
                           struct linux_dmabuf_buffer *buffer)
{
    return true;
}

void hdi_renderer_query_dmabuf_formats(struct weston_compositor *compositor,
                                  int **formats, int *num_formats)
{
    *num_formats = 0;
    *formats = NULL;
}

void hdi_renderer_query_dmabuf_modifiers(struct weston_compositor *compositorc,
                                    int format,
                                    uint64_t **modifiers,
                                    int *num_modifiers)
{
    *num_modifiers = 0;
    *modifiers = NULL;
}

int hdi_renderer_read_pixels(struct weston_output *output,
            pixman_format_code_t format, void *pixels,
            uint32_t x, uint32_t y,
            uint32_t width, uint32_t height)
{
    BufferHandle *bh = hdi_output_get_framebuffer(output);
    int32_t bpp = bh->stride / bh->width;
    int32_t stride = bh->stride;
    int32_t offset = 0;

    if (output->compositor->capabilities & WESTON_CAP_CAPTURE_YFLIP) {
        for (int32_t j = y + height - 1; j >= (int32_t)y; j--) {
            memcpy((uint8_t *)pixels + offset,
                   (uint8_t *)bh->virAddr + j * stride + x * bpp, width * bpp);
            offset += width * bpp;
        }
    } else {
        if (x == 0 && width == bh->width) {
            memcpy(pixels, (uint8_t *)bh->virAddr + y * stride, height * stride);
            return 0;
        }

        for (int32_t j = y; j < y + height; j++) {
            memcpy((uint8_t *)pixels + offset,
                   (uint8_t *)bh->virAddr + j * stride + x * bpp, width * bpp);
            offset += width * bpp;
        }
    }
    return 0;
}

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

typedef void(*weston_view_compute_global_region_func)(struct weston_view *view,
    float x, float y, float *vx, float *vy);

void weston_view_compute_global_region(struct weston_view *view,
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

void weston_view_to_global_region(struct weston_view *view,
                             pixman_region32_t *outr,
                             pixman_region32_t *inr)
{
    weston_view_compute_global_region(view, outr, inr, weston_view_to_global_float);
}

void weston_view_from_global_region(struct weston_view *view,
                               pixman_region32_t *outr,
                               pixman_region32_t *inr)
{
    weston_view_compute_global_region(view, outr, inr, weston_view_from_global_float);
}

void hdi_renderer_repaint_output_calc_region(pixman_region32_t *global_repaint_region,
                                        pixman_region32_t *buffer_repaint_region,
                                        pixman_region32_t *output_damage,
                                        struct weston_output *output,
                                        struct weston_view *view)
{
    struct weston_matrix matrix = output->inverse_matrix;
    if (view->transform.enabled) {
        weston_matrix_multiply(&matrix, &view->transform.inverse);
        LOG_INFO("transform enabled");
    } else {
        weston_matrix_translate(&matrix,
                -view->geometry.x, -view->geometry.y, 0);
        LOG_INFO("transform disabled");
    }
    weston_matrix_multiply(&matrix, &view->surface->buffer_to_surface_matrix);

    auto hss = get_surface_state(view->surface);
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

    pixman_region32_t buffer_region;
    pixman_region32_t surface_repaint_region;
    struct weston_buffer *buffer = view->surface->buffer_ref.buffer;
    pixman_region32_init_rect(&buffer_region, 0, 0, buffer->width, buffer->height);
    pixman_region32_init(&surface_repaint_region);

    LOG_REGION(1, &buffer_region);
    LOG_REGION(2, &view->transform.boundingbox);
    pixman_region32_intersect(global_repaint_region, &view->transform.boundingbox, output_damage);
    LOG_REGION(3, global_repaint_region);

    weston_matrix_transform_region(&surface_repaint_region, &view->transform.inverse, global_repaint_region);
    LOG_REGION(4, &surface_repaint_region);

    weston_surface_to_buffer_region(view->surface, &surface_repaint_region, buffer_repaint_region);
    LOG_REGION(5, buffer_repaint_region);

    pixman_region32_intersect(buffer_repaint_region, buffer_repaint_region, &buffer_region);
    LOG_REGION(6, buffer_repaint_region);

    pixman_region32_translate(global_repaint_region, -output->x, -output->y);
    LOG_REGION(7, global_repaint_region);

    pixman_region32_fini(&surface_repaint_region);
    pixman_region32_fini(&buffer_region);
}

void hdi_renderer_surface_state_calc_rect(struct hdi_surface_state *hss,
    pixman_region32_t *output_damage, struct weston_output *output, struct weston_view *view)
{
    pixman_region32_t global_repaint_region;
    pixman_region32_t buffer_repaint_region;
    pixman_region32_init(&global_repaint_region);
    pixman_region32_init(&buffer_repaint_region);

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

int hdi_renderer_surface_state_create_layer(struct hdi_surface_state *hss,
    struct hdi_backend *b, struct weston_output *output)
{
    struct weston_mode *mode = output->current_mode;
    struct weston_head *whead = weston_output_get_first_head(output);
    auto device_id = hdi_head_get_device_id(whead);
    auto it = hss->layer_ids.find(device_id);
    if (it == hss->layer_ids.end()) {
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
        int ret = b->layer_funcs->CreateLayer(device_id,
                                              &hss->layer_info, &hss->layer_ids[device_id]);
        LOG_CORE("LayerFuncs.CreateLayer return %d", ret);
        if (ret != DISPLAY_SUCCESS) {
            LOG_ERROR("create layer failed");
            hss->layer_ids.erase(device_id);
            return -1;
        }
        LOG_IMPORTANT("create layer: {%d:%d}", device_id, hss->layer_ids[device_id]);
    } else {
        LOG_IMPORTANT("use layer: {%d:%d}", device_id, it->second);
    }
    return 0;
}

void hdi_renderer_repaint_output(struct weston_output *output,
                            pixman_region32_t *output_damage)
{
    LOG_SCOPE();
    struct weston_compositor *compositor = output->compositor;
    struct hdi_backend *b = to_hdi_backend(compositor);
    struct weston_head *whead = weston_output_get_first_head(output);
    uint32_t device_id = hdi_head_get_device_id(whead);
    auto ho = get_output_state(output);
    auto old_layers = ho->layers;
    ho->layers.clear();

    int32_t zorder = 2;
    struct weston_view *view;
    pixman_region32_t repaint;
    wl_list_for_each_reverse(view, &compositor->view_list, link) {
        if (view->renderer_type != WESTON_RENDERER_TYPE_HDI) {
            continue;
        }
        pixman_region32_init(&repaint);
        pixman_region32_intersect(&repaint,
                        &view->transform.boundingbox, output_damage);
        pixman_region32_subtract(&repaint, &repaint, &view->clip);

        if (!pixman_region32_not_empty(&repaint)) {
            continue;
        }

        auto hss = get_surface_state(view->surface);
        if (hss == NULL) {
            continue;
        }

        if (hdi_renderer_surface_state_create_layer(hss, b, output) != 0) {
            continue;
        }

        ho->layers.insert(hss);
        hss->hos[device_id] = ho;

        hdi_renderer_surface_state_calc_rect(hss, output_damage, output, view);
        hss->zorder = zorder++;
        hss->blend_type = BLEND_SRCOVER;
        if (hss->surface->type == WL_SURFACE_TYPE_VIDEO) {
            hss->comp_type = COMPOSITION_VIDEO;
            hss->zorder += 100;
        } else {
            hss->comp_type = COMPOSITION_DEVICE;
        }
    }

    // close not composite layer
    for (auto &hss : old_layers) {
        if (ho->layers.find(hss) == ho->layers.end()) {
            hdi_renderer_layer_close(b, device_id, hss->layer_ids[device_id]);
            hss->layer_ids.erase(device_id);
        }
    }

    wl_list_for_each_reverse(view, &compositor->view_list, link) {
        if (view->renderer_type != WESTON_RENDERER_TYPE_HDI) {
            continue;
        }
        pixman_region32_init(&repaint);
        pixman_region32_intersect(&repaint,
                        &view->transform.boundingbox, output_damage);
        pixman_region32_subtract(&repaint, &repaint, &view->clip);

        if (!pixman_region32_not_empty(&repaint)) {
            continue;
        }

        LOG_INFO("LayerOperation: %p", view);
        auto hss = get_surface_state(view->surface);
        if (hss == NULL) {
            continue;
        }

        if (hdi_renderer_surface_state_create_layer(hss, b, output) != 0) {
            continue;
        }

        b->layer_dump_info_pending[device_id][hss->layer_ids[device_id]].view = view;
        BufferHandle *bh = nullptr;
        if (hss->surface->type != WL_SURFACE_TYPE_VIDEO) {
            bh = hdi_renderer_surface_state_mmap(hss);
        }

        LayerAlpha alpha = { .enPixelAlpha = true };
        hdi_renderer_layer_operation(b, device_id, hss->layer_ids[device_id],
                                     bh, -1,
                                     &alpha,
                                     &hss->dst_rect,
                                     &hss->src_rect,
                                     hss->zorder,
                                     hss->blend_type,
                                     hss->comp_type,
                                     hss->rotate_type);
    }
    pixman_region32_fini(&repaint);
}

void hdi_renderer_surface_set_color(struct weston_surface *surface,
                               float red, float green,
                               float blue, float alpha)
{
}

void hdi_renderer_surface_get_content_size(struct weston_surface *surface,
                               int *width, int *height)
{
    auto hss = get_surface_state(surface);
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

int hdi_renderer_surface_copy_content(struct weston_surface *surface,
                                  void *target, size_t size,
                                  int src_x, int src_y, int width, int height)
{
    auto hss = get_surface_state(surface);
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

int hdi_renderer_init(struct weston_compositor *compositor)
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

int hdi_renderer_output_create(struct weston_output *output,
    const struct hdi_renderer_output_options *options)
{
    LOG_SCOPE();
    auto ho = new struct hdi_output_state();
    ho->gpu_layer_id = -1;
    output->hdi_renderer_state = ho;
    return 0;
}

void hdi_renderer_output_destroy(struct weston_output *output)
{
    LOG_SCOPE();
    auto ho = (struct hdi_output_state *)output->hdi_renderer_state;
    if (ho->gpu_layer_id == DISPLAY_SUCCESS) {
        struct hdi_backend *b = to_hdi_backend(output->compositor);
        struct weston_head *whead = weston_output_get_first_head(output);
        uint32_t device_id = hdi_head_get_device_id(whead);
        hdi_renderer_layer_close(b, device_id, ho->gpu_layer_id);
    }

    delete ho;
}

void hdi_renderer_output_set_gpu_buffer(struct weston_output *output, BufferHandle *buffer)
{
    LOG_SCOPE();
    struct hdi_backend *b = to_hdi_backend(output->compositor);
    struct hdi_output_state *ho =
        (struct hdi_output_state *)output->hdi_renderer_state;
    struct weston_head *whead = weston_output_get_first_head(output);
    int32_t device_id = hdi_head_get_device_id(whead);

    // close last gpu layer
    if (ho->gpu_layer_id != -1) {
        hdi_renderer_layer_close(b, device_id, ho->gpu_layer_id);
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
    LOG_CORE("LayerFuncs.CreateLayer GPU return %d", ret);
    if (ret != DISPLAY_SUCCESS) {
        LOG_ERROR("create layer failed");
        ho->gpu_layer_id = -1;
        return;
    }
    LOG_INFO("create layer GPU {%d:%d}", device_id, ho->gpu_layer_id);

    // param
    LayerAlpha alpha = { .enPixelAlpha = true };
    int32_t fence = -1;
    IRect dst_rect = { .w = buffer->width, .h = buffer->height, };
    IRect src_rect = dst_rect;

    // layer operation
    hdi_renderer_layer_operation(b, device_id, ho->gpu_layer_id,
                                 buffer, -1,
                                 &alpha,
                                 &dst_rect,
                                 &src_rect,
                                 1, // 1 for gpu
                                 BLEND_SRC,
                                 COMPOSITION_DEVICE,
                                 ROTATE_NONE);
}
