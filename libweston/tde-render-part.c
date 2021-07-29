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

#include "tde-render-part.h"

#include <securec.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <display_gfx.h>
#include <display_gralloc.h>
#include <display_layer.h>
#include <display_type.h>
#include <dlfcn.h>
#include <drm.h>
#include <drm/drm_fourcc.h>
#include <drm-internal.h>
#include <linux-dmabuf.h>
#include <linux-dmabuf-unstable-v1-server-protocol.h>
#include <linux-explicit-synchronization.h>
#include <sys/mman.h>

#include "libweston/weston-log.h"
#include "pixman-renderer-protected.h"

struct tde_image_t {
    int32_t width;
    int32_t height;
    uint32_t format;
    int n_planes; // 默认为1，引入plane是为了保证方案的通用性
    __u64 phyaddr; // 物理地址
    int fd[MAX_DMABUF_PLANES];
    uint32_t offset[MAX_DMABUF_PLANES];
    uint32_t stride[MAX_DMABUF_PLANES];
};

struct tde_output_state_t {
    struct tde_image_t image;
};

struct tde_surface_state_t {
    struct tde_image_t image;
};

struct tde_renderer_t {
    GfxFuncs *gfx_funcs;
    int use_tde;
};

struct drm_hisilicon_phy_addr {
    uint64_t phyaddr; // return the phy addr
    int fd; // dmabuf file descriptor
};

#define DRM_HISILICON_GEM_FD_TO_PHYADDR (0x1)
#define DRM_IOCTL_HISILICON_GEM_FD_TO_PHYADDR \
    (DRM_IOWR(DRM_COMMAND_BASE + DRM_HISILICON_GEM_FD_TO_PHYADDR, \
        struct drm_hisilicon_phy_addr))

static uint64_t drm_fd_phyaddr(struct weston_compositor *compositor, int fd)
{
    struct drm_backend *backend = to_drm_backend(compositor);
    struct drm_hisilicon_phy_addr args = { .fd = fd };

    int ret = ioctl(backend->drm.fd,
            DRM_IOCTL_HISILICON_GEM_FD_TO_PHYADDR, &args);
    return args.phyaddr;
}

static void drm_close_handle(struct weston_compositor *compositor, int fd)
{
    struct drm_backend *backend = to_drm_backend(compositor);
    uint32_t gem_handle;
    int ret = drmPrimeFDToHandle(backend->drm.fd , fd, &gem_handle);
    if (ret) {
        weston_log("Failed to PrimeFDToHandle gem handle");
        return;
    }

    struct drm_gem_close gem_close = { .handle = gem_handle };
    ret = drmIoctl(backend->drm.fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
    if (ret) {
        weston_log("Failed to close gem handle");
        return;
    }
}

static uint64_t dst_image_phyaddr(struct weston_output *wo)
{
    struct drm_output *output = to_drm_output(wo);
    struct drm_backend *backend = to_drm_backend(wo->compositor);

    int prime_fd;
    int ret = drmPrimeHandleToFD(backend->drm.fd,
        output->dumb[output->current_image]->handles[0],
        DRM_CLOEXEC, &prime_fd);

    uint64_t phyaddr = drm_fd_phyaddr(wo->compositor, prime_fd);
    close(prime_fd);
    return phyaddr;
}

static void src_surface_init(ISurface *surface, struct tde_image_t buffer)
{
    surface->width = buffer.width;
    surface->height = buffer.height;
    surface->phyAddr = buffer.phyaddr;
    surface->stride = buffer.stride[0];
    surface->enColorFmt = PIXEL_FMT_RGBA_8888;
    surface->bAlphaExt1555 = true;
    surface->bAlphaMax255 = true;
    surface->alpha0 = 0XFF;
    surface->alpha1 = 0XFF;
}

static void dst_surface_init(ISurface *surface, pixman_image_t *target_image,
                             struct weston_output *output)
{
    surface->width = pixman_image_get_width(target_image);
    surface->height = pixman_image_get_height(target_image);
    surface->phyAddr = dst_image_phyaddr(output);
    surface->enColorFmt = PIXEL_FMT_BGRA_8888;
    surface->stride = pixman_image_get_stride(target_image);
    surface->bAlphaExt1555 = true;
    surface->bAlphaMax255 = true;
    surface->alpha0 = 0XFF;
    surface->alpha1 = 0XFF;
}
static IRect get_irect_from_box32(pixman_region32_t *region32)
{
    pixman_box32_t box32 = *pixman_region32_extents(region32);
    IRect Rect = {box32.x1, box32.y1, box32.x2 - box32.x1, box32.y2 - box32.y1};
    return Rect;
}

static __inline int32_t min(int32_t x, int32_t y)
{
    return x < y ? x : y;
}

static int tde_repaint_region(struct weston_view *ev,
                              struct weston_output *output,
                              pixman_region32_t *buffer_region,
                              pixman_region32_t *repaint_output)
{
    struct pixman_renderer *renderer = output->compositor->renderer;
    struct pixman_surface_state *surface = get_surface_state(ev->surface);
    struct pixman_output_state *output_state = get_output_state(output);
    float view_x = 0;
    float view_y = 0;
    weston_view_to_global_float(ev, 0, 0, &view_x, &view_y);
    IRect IdstRect = get_irect_from_box32(repaint_output);
    IRect IsrcRect = get_irect_from_box32(buffer_region);
    int ret = 0;
    ret = renderer->tde->gfx_funcs->InitGfx();
    if (ret) {
        return -1;
    }
    pixman_image_t *target_image = output_state->hw_buffer;
    if (output_state->shadow_image) {
        target_image = output_state->shadow_image;
    }
    ISurface dstSurface = {};
    dst_surface_init(&dstSurface, target_image, output);
    if (ev->surface->type == WL_SURFACE_TYPE_VIDEO) {
        GfxOpt opt = {
            .blendType = BLEND_SRC,
            .enableScale = true,
            .enPixelAlpha = true,
        };
        renderer->tde->gfx_funcs->FillRect(&dstSurface, &IdstRect,
                                           0x00000000, &opt);
    } else {
        GfxOpt opt = {
            .blendType = BLEND_SRCOVER,
            .enableScale = true,
            .enPixelAlpha = true,
        };
        IsrcRect.x += IdstRect.x - view_x;
        IsrcRect.y += IdstRect.y - view_y;
        IdstRect.w = min(IsrcRect.w, IdstRect.w);
        IsrcRect.w = min(IsrcRect.w, IdstRect.w);
        IdstRect.h = min(IsrcRect.h, IdstRect.h);
        IsrcRect.h = min(IsrcRect.h, IdstRect.h);
        ISurface srcSurface = {};
        src_surface_init(&srcSurface, surface->tde->image);
        ret = renderer->tde->gfx_funcs->Blit(&srcSurface, &IsrcRect, &dstSurface, &IdstRect, &opt);
    }
    ret = renderer->tde->gfx_funcs->DeinitGfx();
    return 0;
}

static bool import_dmabuf(struct weston_compositor *ec,
                          struct linux_dmabuf_buffer *dmabuf)
{
    return true;
}

static void query_dmabuf_formats(struct weston_compositor *wc,
                                 int **formats, int *num_formats)
{
    static const int fallback_formats[] = {
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_YUYV,
        DRM_FORMAT_NV12,
        DRM_FORMAT_YUV420,
        DRM_FORMAT_YUV444,
    };
    int num = ARRAY_LENGTH(fallback_formats);
    if (num > 0) {
        *formats = calloc(num, sizeof(int));
    }

    memcpy_s(*formats, num * sizeof(int), fallback_formats, sizeof(fallback_formats));
    *num_formats = num;
}

static void query_dmabuf_modifiers(struct weston_compositor *wc, int format,
                                   uint64_t **modifiers, int *num_modifiers)
{
    *modifiers = NULL;
    *num_modifiers = 0;
}

int tde_renderer_alloc_hook(struct pixman_renderer *renderer, struct weston_compositor *ec)
{
    renderer->tde = zalloc(sizeof(*renderer->tde));
    if (renderer->tde == NULL) {
        return -1;
    }

    struct drm_backend *backend = to_drm_backend(ec);
    if (!backend->use_tde) {
        renderer->tde->use_tde = 0;
    } else {
        int ret = GfxInitialize(&renderer->tde->gfx_funcs);
        renderer->tde->use_tde = (ret == 0 && renderer->tde->gfx_funcs != NULL) ? 1 : 0;
    }
    weston_log("use_tde: %{public}d", renderer->tde->use_tde);

    renderer->base.import_dmabuf = import_dmabuf;
    renderer->base.query_dmabuf_formats = query_dmabuf_formats;
    renderer->base.query_dmabuf_modifiers = query_dmabuf_modifiers;
    return 0;
}

int tde_renderer_free_hook(struct pixman_renderer *renderer)
{
    GfxUninitialize(&renderer->tde->gfx_funcs);
    free(renderer->tde);
    return 0;
}

int tde_output_state_alloc_hook(struct pixman_output_state *state)
{
    state->tde = zalloc(sizeof(*state->tde));
    return 0;
}

int tde_output_state_free_hook(struct pixman_output_state *state)
{
    free(state->tde);
    return 0;
}

int tde_surface_state_alloc_hook(struct pixman_surface_state *state)
{
    state->tde = zalloc(sizeof(*state->tde));
    return 0;
}

int tde_surface_state_free_hook(struct pixman_surface_state *state)
{
    free(state->tde);
    return 0;
}

static void buffer_state_handle_buffer_destroy(struct wl_listener *listener,
                                               void *data)
{
    struct pixman_surface_state *ps = container_of(listener,
        struct pixman_surface_state, buffer_destroy_listener);
    if (ps->image) {
        tde_unref_image_hook(ps->image);

        pixman_image_unref(ps->image);
        ps->image = NULL;
    }

    ps->buffer_destroy_listener.notify = NULL;
}

int tde_render_attach_hook(struct weston_surface *es, struct weston_buffer *buffer)
{
    if (!buffer) {
        return -1;
    }

    struct linux_dmabuf_buffer *dmabuf = linux_dmabuf_buffer_get(buffer->resource);
    if (!dmabuf) {
        return -1;
    }

    struct pixman_surface_state *ps = get_surface_state(es);
    weston_buffer_reference(&ps->buffer_ref, buffer);
    weston_buffer_release_reference(&ps->buffer_release_ref,
            es->buffer_release_ref.buffer_release);

    if (ps->buffer_destroy_listener.notify) {
        wl_list_remove(&ps->buffer_destroy_listener.link);
        ps->buffer_destroy_listener.notify = NULL;
    }

    if (ps->image) {
        tde_unref_image_hook(ps->image);

        pixman_image_unref(ps->image);
        ps->image = NULL;
    }

    pixman_format_code_t pixman_format = PIXMAN_a8r8g8b8;
    buffer->legacy_buffer = NULL;
    buffer->width = dmabuf->attributes.width;
    buffer->height = dmabuf->attributes.height;
    int stride = dmabuf->attributes.stride[0];
    int fd = dmabuf->attributes.fd[0];

    ps->tde->image.width = dmabuf->attributes.width;
    ps->tde->image.height = dmabuf->attributes.height;
    ps->tde->image.stride[0] = dmabuf->attributes.stride[0];
    ps->tde->image.format = dmabuf->attributes.format;
    ps->tde->image.fd[0] = fd;

    uint32_t* ptr = (uint32_t*)mmap(NULL, stride * buffer->height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct pixman_renderer *pr = get_renderer(es->compositor);
    if (pr->tde->use_tde) {
        ps->tde->image.phyaddr = drm_fd_phyaddr(es->compositor, fd);
        drm_close_handle(es->compositor, fd);
        if (ps->tde->image.phyaddr == 0) {
            if (ptr) {
                munmap(ptr, stride * buffer->height);
            }
            return 0;
        }
    }

    ps->image = pixman_image_create_bits(pixman_format, buffer->width, buffer->height, ptr, stride);

    ps->buffer_destroy_listener.notify = buffer_state_handle_buffer_destroy;
    wl_signal_add(&buffer->destroy_signal, &ps->buffer_destroy_listener);
    return 0;
}

int tde_repaint_region_hook(struct weston_view *ev, struct weston_output *output,
        pixman_region32_t *buffer_region, pixman_region32_t *repaint_output)
{
    struct pixman_renderer *renderer = output->compositor->renderer;
    if (!renderer->tde->use_tde) {
        return -1;
    }

    return tde_repaint_region(ev, output, buffer_region, repaint_output);
}

int tde_unref_image_hook(pixman_image_t *image)
{
    if (image == NULL) {
        return 0;
    }

    int height = pixman_image_get_height(image);
    int stride = pixman_image_get_stride(image);
    void *ptr = pixman_image_get_data(image);
    if (ptr) {
        munmap(ptr, height * stride);
    }

    return 0;
}
