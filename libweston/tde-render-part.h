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

#ifndef LIBWESTON_TDE_RENDER_PART_H
#define LIBWESTON_TDE_RENDER_PART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pixman-renderer-protected.h"

// hook return 0: success, other: failure
int tde_renderer_alloc_hook(struct pixman_renderer *renderer, struct weston_compositor *ec);
int tde_renderer_free_hook(struct pixman_renderer *renderer);

int tde_output_state_alloc_hook(struct pixman_output_state *state);
int tde_output_state_init_hook(struct pixman_output_state *state);
int tde_output_state_free_hook(struct pixman_output_state *state);

int tde_surface_state_alloc_hook(struct pixman_surface_state *state);
int tde_surface_state_free_hook(struct pixman_surface_state *state);

int tde_render_attach_hook(struct weston_surface *es, struct weston_buffer *buffer);

int tde_repaint_region_hook(struct weston_view *ev, struct weston_output *output,
                         pixman_region32_t *buffer_region,
                         pixman_region32_t *repaint_output);
void tde_repaint_finish_hook(struct weston_output *output);

int tde_unref_image_hook(struct pixman_surface_state *ps);

#ifdef __cplusplus
}
#endif

#endif // LIBWESTON_TDE_RENDER_PART_H
