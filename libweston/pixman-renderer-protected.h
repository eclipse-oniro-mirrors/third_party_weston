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

#include "pixman-renderer.h"

#ifndef LIBWESTON_PIXMAN_RENDERER_PROTECTED_H
#define LIBWESTON_PIXMAN_RENDERER_PROTECTED_H

struct pixman_output_state {
	void *shadow_buffer;
	pixman_image_t *shadow_image;
	pixman_image_t *hw_buffer;
	pixman_region32_t *hw_extra_damage;
    struct tde_output_state_t *tde;
};

struct pixman_surface_state {
	struct weston_surface *surface;

	pixman_image_t *image;
	struct weston_buffer_reference buffer_ref;
	struct weston_buffer_release_reference buffer_release_ref;

	struct wl_listener buffer_destroy_listener;
	struct wl_listener surface_destroy_listener;
	struct wl_listener renderer_destroy_listener;
    struct tde_surface_state_t *tde;
};

struct pixman_renderer {
	struct weston_renderer base;

	int repaint_debug;
	pixman_image_t *debug_color;
	struct weston_binding *debug_binding;

	struct wl_signal destroy_signal;
    struct tde_renderer_t *tde;
};

struct pixman_surface_state * get_surface_state(struct weston_surface *surface);
struct pixman_renderer * get_renderer(struct weston_compositor *ec);
struct pixman_output_state * get_output_state(struct weston_output *output);

#endif // LIBWESTON_PIXMAN_RENDERER_PROTECTED_H
