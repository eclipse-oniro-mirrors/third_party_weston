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

#include "mix_renderer.h"

// C header adapter
extern "C" {
#include "libweston/libweston.h"
#include "shared/helpers.h"
}

#include "libweston/trace.h"
DEFINE_LOG_LABEL("MixRenderer");

struct mix_renderer {
    struct weston_renderer base;
};

static void
mix_renderer_attach(struct weston_surface *surface,
                    struct weston_buffer *buffer)
{
    surface->compositor->hdi_renderer->attach(surface, buffer);
    if (surface->compositor->gpu_renderer) {
        surface->compositor->gpu_renderer->attach(surface, buffer);
    }
}

static void
mix_renderer_destroy(struct weston_compositor *compositor)
{
    LOG_PASS();
    delete reinterpret_cast<struct mix_renderer *>(compositor->renderer);
    compositor->renderer = NULL;

    if (compositor->gpu_renderer) {
        compositor->gpu_renderer->destroy(compositor);
    }

    if (compositor->hdi_renderer) {
        compositor->hdi_renderer->destroy(compositor);
    }
}

static void
mix_renderer_flush_damage(struct weston_surface *surface)
{
    if (surface->compositor->gpu_renderer) {
        surface->compositor->gpu_renderer->flush_damage(surface);
    } else {
        surface->compositor->hdi_renderer->flush_damage(surface);
    }
}

static bool
mix_renderer_import_dmabuf(struct weston_compositor *compositor,
                           struct linux_dmabuf_buffer *buffer)
{
    struct weston_renderer *renderer = compositor->gpu_renderer;
    if (!renderer) {
        renderer = compositor->hdi_renderer;
    }

    if (renderer->import_dmabuf == NULL)
        return false;

    return renderer->import_dmabuf(compositor, buffer);
}

static void
mix_renderer_query_dmabuf_formats(struct weston_compositor *compositor,
                                  int **formats, int *num_formats)
{
    compositor->hdi_renderer->query_dmabuf_formats(compositor, formats, num_formats);
}

static void
mix_renderer_query_dmabuf_modifiers(struct weston_compositor *compositor,
                                    int format,
                                    uint64_t **modifiers,
                                    int *num_modifiers)
{
    compositor->hdi_renderer->query_dmabuf_modifiers(compositor, format, modifiers, num_modifiers);
}

static int
mix_renderer_read_pixels(struct weston_output *output,
            pixman_format_code_t format, void *pixels,
            uint32_t x, uint32_t y,
            uint32_t width, uint32_t height)
{
    return output->compositor->hdi_renderer->read_pixels(output, format, pixels, x, y, width, height);
}

static void
mix_renderer_repaint_output(struct weston_output *output,
                            pixman_region32_t *output_damage)
{
    output->compositor->hdi_renderer->repaint_output(output, output_damage);
}

static void
mix_renderer_surface_set_color(struct weston_surface *surface,
                               float red, float green,
                               float blue, float alpha)
{
    if (surface->compositor->gpu_renderer) {
        surface->compositor->gpu_renderer->surface_set_color(surface, red, green, blue, alpha);
    } else {
        surface->compositor->hdi_renderer->surface_set_color(surface, red, green, blue, alpha);
    }
}

static void
mix_renderer_surface_get_content_size(struct weston_surface *surface,
                               int *width, int *height)
{
    surface->compositor->hdi_renderer->surface_get_content_size(surface, width, height);
}

static int
mix_renderer_surface_copy_content(struct weston_surface *surface,
                                  void *target, size_t size,
                                  int src_x, int src_y, int width, int height)
{
    return surface->compositor->hdi_renderer->surface_copy_content(surface, target, size,
                                                                   src_x, src_y, width, height);
}

int
mix_renderer_init(struct weston_compositor *compositor)
{
    LOG_PASS();
    auto renderer = new struct mix_renderer();
    if (renderer == NULL) {
        return 1;
    }

    renderer->base.attach = mix_renderer_attach;
    renderer->base.destroy = mix_renderer_destroy;
    renderer->base.flush_damage = mix_renderer_flush_damage;
    renderer->base.import_dmabuf = mix_renderer_import_dmabuf;
    renderer->base.query_dmabuf_formats = mix_renderer_query_dmabuf_formats;
    renderer->base.query_dmabuf_modifiers = mix_renderer_query_dmabuf_modifiers;
    renderer->base.read_pixels = mix_renderer_read_pixels;
    renderer->base.repaint_output = mix_renderer_repaint_output;
    renderer->base.surface_set_color = mix_renderer_surface_set_color;
    renderer->base.surface_get_content_size = mix_renderer_surface_get_content_size;
    renderer->base.surface_copy_content = mix_renderer_surface_copy_content;

    compositor->renderer = &renderer->base;
    return 0;
}
