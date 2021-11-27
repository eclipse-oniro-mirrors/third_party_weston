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

#ifndef SHARED_SIMPLE_GBM_H
#define SHARED_SIMPLE_GBM_H

#ifdef __cplusplus
extern "C" {
#endif

struct gbm_device;

// gbm device
struct gbm_device *gbm_create_device(int fd);
void gbm_device_destroy(struct gbm_device *gbm);

// gbm bo
struct gbm_bo * gbm_bo_create(struct gbm_device *gbm,
                              uint32_t width, uint32_t height,
                              uint32_t format, uint32_t flags);
struct gbm_bo * gbm_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer, uint32_t usage);
void * gbm_bo_get_user_data(struct gbm_bo *bo);
union gbm_bo_handle {
   void *ptr;
   int32_t s32;
   uint32_t u32;
   int64_t s64;
   uint64_t u64;
};
union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo);
void gbm_bo_destroy(struct gbm_bo *bo);

// gbm surface
struct gbm_surface * gbm_surface_create(struct gbm_device *gbm,
                                        uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t flags);


enum gbm_bo_flags {
   /**
    * Buffer is going to be presented to the screen using an API such as KMS
    */
   GBM_BO_USE_SCANOUT      = (1 << 0),
   /**
    * Buffer is going to be used as cursor
    */
   GBM_BO_USE_CURSOR       = (1 << 1),
   /**
    * Deprecated
    */
   GBM_BO_USE_CURSOR_64X64 = GBM_BO_USE_CURSOR,
   /**
    * Buffer is to be used for rendering - for example it is going to be used
    * as the storage for a color buffer
    */
   GBM_BO_USE_RENDERING    = (1 << 2),
   /**
    * Buffer can be used for gbm_bo_write.  This is guaranteed to work
    * with GBM_BO_USE_CURSOR, but may not work for other combinations.
    */
   GBM_BO_USE_WRITE    = (1 << 3),
   /**
    * Buffer is linear, i.e. not tiled.
    */
   GBM_BO_USE_LINEAR = (1 << 4),
   /**
    * Buffer is protected, i.e. encrypted and not readable by CPU or any
    * other non-secure / non-trusted components nor by non-trusted OpenGL,
    * OpenCL, and Vulkan applications.
    */
   GBM_BO_USE_PROTECTED = (1 << 5),
};

struct gbm_import_fd_data {
   int fd;
   uint32_t width;
   uint32_t height;
   uint32_t stride;
   uint32_t format;
};

#define GBM_BO_IMPORT_WL_BUFFER         0x5501
#define GBM_BO_IMPORT_EGL_IMAGE         0x5502
#define GBM_BO_IMPORT_FD                0x5503
#define GBM_BO_IMPORT_FD_MODIFIER       0x5504

#define __gbm_fourcc_code(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
			      ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define GBM_FORMAT_ARGB8888	__gbm_fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */

#ifdef __cplusplus
}
#endif

#endif // SHARED_SIMPLE_GBM_H
