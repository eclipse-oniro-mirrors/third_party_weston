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

#include <memory.h>

#include "libweston/libweston.h"
#include "libweston/backend-hdi.h"

#include "hdi_backend.h"

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiModule");

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
            struct weston_backend_config *config_base)
{
    LOG_ENTER();
    if (config_base == NULL ||
        config_base->struct_version != WESTON_HDI_BACKEND_CONFIG_VERSION ||
        config_base->struct_size > sizeof(struct weston_hdi_backend_config)) {
        weston_log("hdi backend config structure is invalid\n");
        LOG_EXIT();
        return -1;
    }

    struct weston_hdi_backend_config config;
    memcpy(&config, config_base, config_base->struct_size);
    struct hdi_backend *b = hdi_backend_create(compositor, &config);
    LOG_EXIT();
    return b != NULL ? 0 : -1;
}
