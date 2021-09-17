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

#ifndef WESTON_COMPOSITOR_HDI_H
#define WESTON_COMPOSITOR_HDI_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "libweston/libweston.h"
#include "libweston/plugin-registry.h"
#include "libweston/libinput-seat.h"

#define WESTON_HDI_BACKEND_CONFIG_VERSION 2
#define WESTON_HDI_OUTPUT_API_NAME "weston_hdi_output_api_v1"

struct weston_hdi_backend_config {
	struct weston_backend_config base;
	bool use_hdi;
	void (*configure_device)(struct weston_compositor *compositor,
				 struct libinput_device *device);
};

struct weston_hdi_output_api {
    int (*set_mode)(struct weston_output *output);
};

static inline const struct weston_hdi_output_api *
weston_hdi_output_get_api(struct weston_compositor *compositor)
{
    return (const struct weston_hdi_output_api *)weston_plugin_api_get(
        compositor, WESTON_HDI_OUTPUT_API_NAME, sizeof(struct weston_hdi_output_api));
}

#ifdef  __cplusplus
}
#endif

#endif /* WESTON_COMPOSITOR_HDI_H */
