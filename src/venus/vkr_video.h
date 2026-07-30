/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_VIDEO_H
#define VKR_VIDEO_H

#include "vkr_common.h"

/* Video session objects.
 *
 * A video session owns something the renderer must track beyond the raw
 * handle: its parameters objects. Destroying a session implicitly destroys
 * them, so the renderer has to cascade or it leaks entries in the object table
 * that name freed driver handles.
 *
 * That makes vkr_descriptor_pool the right precedent rather than
 * vkr_query_pool: query pools have no children and no cross-object lifetime.
 * Children link through base.track_head so vkr_context_remove_objects() can
 * reap them, exactly as descriptor sets hang off their pool.
 *
 * This is the feasibility spike. Only the tracking needed to keep the object
 * graph consistent is present. The validation surface -- DPB integrity, coding
 * scope, aggregate resource caps, generation-tagged handles, two-pass handle
 * replacement -- is deliberately absent and lands with the hardened
 * implementation, against a shape this spike proves works.
 */

struct vkr_video_session {
   struct vkr_object base;

   /* parameters objects created against this session */
   struct list_head parameters;
};
VKR_DEFINE_OBJECT_CAST(video_session, VK_OBJECT_TYPE_VIDEO_SESSION_KHR, VkVideoSessionKHR)

struct vkr_video_session_parameters {
   struct vkr_object base;
};
VKR_DEFINE_OBJECT_CAST(video_session_parameters,
                       VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR,
                       VkVideoSessionParametersKHR)

void
vkr_context_init_video_dispatch(struct vkr_context *ctx);

#endif /* VKR_VIDEO_H */
