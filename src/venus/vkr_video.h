/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_VIDEO_H
#define VKR_VIDEO_H

#include "vkr_common.h"

/* Video session objects.
 *
 * Sessions and parameters objects are SIBLINGS owned by the device, not a
 * parent and its children. Destroying a video session does NOT implicitly
 * destroy the parameters objects created against it.
 *
 * The spike modelled them as parent/child on the vkr_descriptor_pool
 * precedent and cascaded on destroy. That was wrong twice over: the cascade
 * freed the renderer's record WITHOUT calling
 * vkDestroyVideoSessionParametersKHR, leaking the host object, and a
 * conformant guest destroying its parameters afterwards found a record that
 * was already gone.
 *
 * Mesa's own common implementation settles the ownership question:
 * struct vk_video_session_parameters (src/vulkan/runtime/vk_video.h) holds no
 * back-pointer to a session, and its destroy unlinks from no parent list. A
 * reference implementation would have to track the relationship in order to
 * free them, if the relationship were owning.
 *
 * What a session DOES need beyond the raw handle is its bound memory and the
 * capability limits the decode path range-checks against.
 */

/* Bounds the H.264 parameter-set id space, which is what makes these capacity
 * limits rather than arbitrary numbers: seq_parameter_set_id is 5-bit and
 * pic_parameter_set_id is 8-bit.
 *
 * These bound what is LIVE, never a lifetime total. A stream legitimately
 * rotates parameter sets through the same ids, so a monotonic counter would
 * reject conformant long playback after enough rotations -- a failure that
 * only appears well past the length of any test clip.
 */
#define VKR_VIDEO_MAX_SPS 32
#define VKR_VIDEO_MAX_PPS 256

/* Bounds live sessions per context. A guest can otherwise loop
 * vkCreateVideoSessionKHR; per-array caps bound one command and say nothing
 * about how many objects accumulate.
 */
#define VKR_VIDEO_MAX_SESSIONS_PER_CONTEXT 64

struct vkr_video_session {
   struct vkr_object base;

   /* Recorded at bind time so liveness is checkable at destroy. */
   uint32_t bound_memory_count;

   /* Captured at create so the decode path can range-check DPB slots without
    * re-querying the host per command.
    */
   uint32_t max_dpb_slots;
   uint32_t max_active_references;
};
VKR_DEFINE_OBJECT_CAST(video_session, VK_OBJECT_TYPE_VIDEO_SESSION_KHR, VkVideoSessionKHR)

struct vkr_video_session_parameters {
   struct vkr_object base;

   /* Monotonic update counter, mirrored from the host so an out-of-order
    * update is rejected before forwarding. The spec requires the guest value
    * to be strictly GREATER than the stored one -- not exactly one more, which
    * would reject a conformant client whose counter advances by more.
    */
   uint32_t update_sequence_count;
};
VKR_DEFINE_OBJECT_CAST(video_session_parameters,
                       VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR,
                       VkVideoSessionParametersKHR)

void
vkr_context_init_video_dispatch(struct vkr_context *ctx);

#endif /* VKR_VIDEO_H */
