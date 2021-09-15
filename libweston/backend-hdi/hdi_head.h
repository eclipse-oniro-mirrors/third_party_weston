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

#ifndef LIBWESTON_BACKEND_HDI_HDI_HEAD
#define LIBWESTON_BACKEND_HDI_HDI_HEAD

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct hdi_head;
struct weston_compositor;
struct weston_head;

int hdi_head_create(struct weston_compositor *compositor, uint32_t device_id);
void hdi_head_destroy(struct weston_head *base);
uint32_t hdi_head_get_device_id(struct weston_head *base);

#ifdef __cplusplus
}
#endif

#endif // LIBWESTON_BACKEND_HDI_HDI_HEAD
