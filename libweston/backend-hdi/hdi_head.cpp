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

#include <assert.h>

#include <vsync_module_c.h>

#include "libweston/soft_vsync.h" // OHOS vsync module

// C header adapter
extern "C" {
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include "shared/helpers.h"
}

#include "hdi_backend.h"
#include "hdi_head.h"

#include "libweston/trace.h"
DEFINE_LOG_LABEL("HdiHead");

struct hdi_head {
    struct weston_head base;
    uint32_t device_id;
    DisplayCapability displayCapability;
};

struct hdi_head *
to_hdi_head(struct weston_head *base)
{
    return container_of(base, struct hdi_head, base);
}

uint32_t
hdi_head_get_device_id(struct weston_head *base)
{
    return to_hdi_head(base)->device_id;
}

static void
hdi_vblank_callback(uint32_t seq, uint64_t ns, void *data)
{
    // OHOS vsync module
    VsyncModuleTrigger();
    SoftVsync::GetInstance().SoftVsyncStop();
}

int
hdi_head_create(struct weston_compositor *compositor, uint32_t device_id)
{
    LOG_ENTER();
    struct hdi_backend *b = to_hdi_backend(compositor);
    assert(b->device_funcs);

    struct hdi_head *head = reinterpret_cast<struct hdi_head *>(zalloc(sizeof *head));
    if (head == NULL) {
        LOG_EXIT();
        return -1;
    }

    head->device_id = device_id;

    // now just support display 0 to give vsync signal
    if (device_id == 0) {
        b->device_funcs->RegDisplayVBlankCallback(device_id, hdi_vblank_callback, NULL);
        b->device_funcs->SetDisplayVsyncEnabled(device_id, true);
    }

    if (b->device_funcs->GetDisplayCapability != NULL) {
        int ret = b->device_funcs->GetDisplayCapability(head->device_id, &head->displayCapability);
        LOG_CORE("DeviceFuncs.GetDisplayCapability return %d", ret);
    } else {
        weston_log("GetDisplayCapability is NULL");
    }

    LOG_INFO("screen: %s, %ux%u", head->displayCapability.name,
             head->displayCapability.phyWidth, head->displayCapability.phyHeight);

    weston_head_init(&head->base, head->displayCapability.name);
    weston_head_set_connection_status(&head->base, true);
    weston_compositor_add_head(compositor, &head->base);

    LOG_EXIT();
    return 0;
}

void
hdi_head_destroy(struct weston_head *base)
{
    LOG_PASS();
    if (hdi_head_get_device_id(base) == 0) {
        struct hdi_backend *b = to_hdi_backend(base->compositor);
        if (b != NULL) {
            b->device_funcs->SetDisplayVsyncEnabled(0, false);
        }
    }
    weston_head_release(base);
    free(to_hdi_head(base));
}
