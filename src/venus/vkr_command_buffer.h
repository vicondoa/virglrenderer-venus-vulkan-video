/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_COMMAND_BUFFER_H
#define VKR_COMMAND_BUFFER_H

#include "vkr_common.h"

#include "vkr_context.h"

struct vkr_command_pool {
   struct vkr_object base;

   struct list_head command_buffers;
};
VKR_DEFINE_OBJECT_CAST(command_pool, VK_OBJECT_TYPE_COMMAND_POOL, VkCommandPool)

struct vkr_command_buffer {
   struct vkr_object base;

   struct vkr_device *device;

   /* Video coding scope, per recording.
    *
    * Decoding outside a coding scope is UNDEFINED BEHAVIOUR per spec, not a
    * guaranteed error return, so on a proprietary driver it can mean
    * device-lost or silent corruption rather than a clean failure. The
    * renderer is therefore the enforcement point.
    *
    * Default false, i.e. outside: the state that denies is the one a
    * zero-initialised object starts in, so a path that forgets to maintain
    * this fails closed.
    */
   bool in_video_coding_scope;

   /* The session the current coding scope was opened with, held as an OBJECT
    * ID rather than a pointer.
    *
    * A pointer would dangle: nothing stops the guest destroying the session
    * while a command buffer still references it, and the decode path would
    * then read freed memory to find the DPB limits. An id costs one hash
    * lookup per decode and cannot dangle -- a destroyed session simply is not
    * found, and the decode is rejected.
    */
   vkr_object_id video_coding_session_id;
};
VKR_DEFINE_OBJECT_CAST(command_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, VkCommandBuffer)

void
vkr_context_init_command_pool_dispatch(struct vkr_context *ctx);

void
vkr_context_init_command_buffer_dispatch(struct vkr_context *ctx);

static inline void
vkr_command_pool_release(struct vkr_context *ctx, struct vkr_command_pool *pool)
{
   vkr_context_remove_objects(ctx, &pool->command_buffers);
}

#endif /* VKR_COMMAND_BUFFER_H */
