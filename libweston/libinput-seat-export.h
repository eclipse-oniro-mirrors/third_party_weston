/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBWESTON_LIBINPUT_SEAT_EXPORT_H
#define LIBWESTON_LIBINPUT_SEAT_EXPORT_H

#include <libinput.h>

// for multi model input
typedef void (*libinput_event_listener)(struct libinput_event *event);
void set_libinput_event_listener(libinput_event_listener listener);

#endif // LIBWESTON_LIBINPUT_SEAT_EXPORT_H_
