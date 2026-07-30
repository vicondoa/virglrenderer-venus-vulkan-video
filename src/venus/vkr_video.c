/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "vkr_video.h"

#include "venus-protocol/vn_protocol_renderer_command_buffer.h"
#include "venus-protocol/vn_protocol_renderer_device.h"
#include "venus-protocol/vn_protocol_renderer_transport.h"

#include "vkr_command_buffer.h"
#include "vkr_context.h"
#include "vkr_device.h"
#include "vkr_physical_device.h"

/* Renderer-side VK_KHR_video_decode_h264 forwarding.
 *
 * Scope: the renderer forwards, the host driver decodes. There is no decoder
 * logic, no bitstream parsing, no NVDEC or V4L2 anywhere in this file, and
 * there must never be.
 *
 * FEASIBILITY SPIKE -- NOT THE HARDENED IMPLEMENTATION.
 *
 * W2 established that this codebase's defining defect is a hand-written set
 * deciding whether a guard applies; it was found twelve times across 23 panel
 * rounds. W3 replaces roughly 95 rejections with roughly 95 validators, which
 * is the same failure mode with the polarity reversed: a wrong rejection fails
 * closed, a wrong validation fails open.
 *
 * This spike deliberately does not attempt those validators. Its single
 * purpose is to answer whether Venus can carry Vulkan Video at all -- DPB
 * image sharing across virtio-gpu, video session memory binding through
 * Venus's memory path, and decode queue family mapping are all unanswered and
 * none of them is testable without executing a decode. Writing the validation
 * surface first would mean writing it against a shape nobody has run.
 *
 * What that means concretely for anyone reading this file: it trusts the guest
 * far more than the hardened implementation will. Handle lookups are checked,
 * because a NULL deref is not a useful experiment, but profile contents,
 * reference slot membership, DPB slot ranges, coding scope, and parameters
 * update sequencing are all forwarded to the host driver unexamined.
 */


/* --- decode activity counters -------------------------------------------
 *
 * The plan's evidence contract requires command-level proof that decode
 * actually executed, correlated to the process that asked for it -- because
 * every layer above this one fails silently. FFmpeg falls back to software and
 * exits 0; Firefox falls back to software and plays an identical picture. A
 * frame count from either is evidence that SOMETHING decoded, not that it
 * decoded here.
 *
 * These counters are the renderer's own statement of what it executed. They
 * are the only place in the stack that cannot be satisfied by a fallback.
 */
static uint64_t vkr_video_session_creates;
static uint64_t vkr_video_decode_cmds;

static void
vkr_video_count_decode(void)
{
   vkr_video_decode_cmds++;
   /* Log on a curve rather than every call: the first few make an experiment
    * observable immediately, and the powers of two keep a long playback from
    * flooding the log while still showing it is progressing.
    */
   if (vkr_video_decode_cmds <= 3 ||
       (vkr_video_decode_cmds & (vkr_video_decode_cmds - 1)) == 0) {
      vkr_log("VIDEO-EVIDENCE decode_cmds=%" PRIu64 " sessions=%" PRIu64,
              vkr_video_decode_cmds, vkr_video_session_creates);
   }
}

/* --- validation ----------------------------------------------------------
 *
 * The guest command stream is untrusted input to a host process holding an
 * open GPU fd. Everything below runs BEFORE the corresponding host call, and
 * every check fails closed.
 *
 * Note the direction of the risk compared with W2. A wrong REJECTION fails
 * closed and shows up as a feature that does not work; a wrong VALIDATION
 * fails open and shows up as nothing at all. That asymmetry is why each of
 * these ships with a negative control rather than only a positive one.
 */

/* Resolve VK_WHOLE_SIZE and bounds-check without overflowing.
 *
 * The obvious `offset + range <= size` wraps in 64-bit arithmetic when range
 * is VK_WHOLE_SIZE (~0ULL), and the check then PASSES for exactly the input it
 * exists to reject. Comparing by subtraction on values already known to be in
 * range cannot wrap.
 */
static bool
vkr_video_range_within(VkDeviceSize offset, VkDeviceSize range, VkDeviceSize size)
{
   if (offset > size)
      return false;
   if (range == VK_WHOLE_SIZE)
      return true; /* resolves to the remainder of the buffer by definition */
   return range <= size - offset;
}

/* A reference slot index is either "not retained in the DPB" or a real slot.
 *
 * slotIndex == -1 legitimately means the picture is not retained, which is
 * ordinary for non-reference pictures in High-profile B-frames. A membership
 * check applied naively to that case rejects valid content.
 */
static bool
vkr_video_slot_index_is_ignored(int32_t slot_index)
{
   return slot_index < 0;
}

static bool
vkr_video_validate_reference_slot(const struct vkr_video_session *sess,
                                  const VkVideoReferenceSlotInfoKHR *slot,
                                  bool is_setup_slot)
{
   if (!slot)
      return true; /* pSetupReferenceSlot may be NULL */

   if (vkr_video_slot_index_is_ignored(slot->slotIndex)) {
      /* The resource is IGNORED when the index is negative, so a conformant
       * guest may leave it uninitialised. Reading it here -- to validate or to
       * replace a handle -- would dereference whatever happened to be there
       * and reject ordinary B-frame content.
       */
      return true;
   }

   if ((uint32_t)slot->slotIndex >= sess->max_dpb_slots)
      return false;

   /* A setup slot with a real index is a WRITE target, so it gets the same
    * treatment as a reference slot. The spike exempted it entirely, which let
    * decoded output land on an image the session never bound.
    */
   if (!slot->pPictureResource)
      return !is_setup_slot;

   return slot->pPictureResource->imageViewBinding != VK_NULL_HANDLE;
}

static bool
vkr_video_validate_decode_info(const struct vkr_video_session *sess,
                               const VkVideoDecodeInfoKHR *info)
{
   if (!info || !sess)
      return false;

   /* Count first: bounding the indices says nothing about how many there are,
    * and the array is walked below.
    */
   if (info->referenceSlotCount > sess->max_dpb_slots)
      return false;

   if (!vkr_video_validate_reference_slot(sess, info->pSetupReferenceSlot, true))
      return false;

   for (uint32_t i = 0; i < info->referenceSlotCount; i++) {
      if (!vkr_video_validate_reference_slot(sess, &info->pReferenceSlots[i], false))
         return false;
   }

   return true;
}

static void
vkr_dispatch_vkGetPhysicalDeviceVideoCapabilitiesKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkGetPhysicalDeviceVideoCapabilitiesKHR *args)
{
   struct vkr_physical_device *physical_dev =
      vkr_physical_device_from_handle(args->physicalDevice);
   struct vn_physical_device_proc_table *vk = &physical_dev->proc_table;

   if (!vk->GetPhysicalDeviceVideoCapabilitiesKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   vn_replace_vkGetPhysicalDeviceVideoCapabilitiesKHR_args_handle(args);
   args->ret = vk->GetPhysicalDeviceVideoCapabilitiesKHR(
      args->physicalDevice, args->pVideoProfile, args->pCapabilities);
}

static void
vkr_dispatch_vkGetPhysicalDeviceVideoFormatPropertiesKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkGetPhysicalDeviceVideoFormatPropertiesKHR *args)
{
   struct vkr_physical_device *physical_dev =
      vkr_physical_device_from_handle(args->physicalDevice);
   struct vn_physical_device_proc_table *vk = &physical_dev->proc_table;

   if (!vk->GetPhysicalDeviceVideoFormatPropertiesKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   vn_replace_vkGetPhysicalDeviceVideoFormatPropertiesKHR_args_handle(args);
   args->ret = vk->GetPhysicalDeviceVideoFormatPropertiesKHR(
      args->physicalDevice, args->pVideoFormatInfo, args->pVideoFormatPropertyCount,
      args->pVideoFormatProperties);
}

static void
vkr_dispatch_vkCreateVideoSessionKHR(struct vn_dispatch_context *dispatch,
                                     struct vn_command_vkCreateVideoSessionKHR *args)
{
   struct vkr_context *ctx = dispatch->data;
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   if (!vk->CreateVideoSessionKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      if (args->pVideoSession)
         *args->pVideoSession = VK_NULL_HANDLE;
      return;
   }

   /* Bound live sessions BEFORE allocating or forwarding. Per-array caps bound
    * one command; nothing else bounds how many sessions accumulate, and each
    * one pins host memory once bound.
    */
   if (ctx->video_session_count >= VKR_VIDEO_MAX_SESSIONS_PER_CONTEXT) {
      args->ret = VK_ERROR_TOO_MANY_OBJECTS;
      if (args->pVideoSession)
         *args->pVideoSession = VK_NULL_HANDLE;
      return;
   }

   struct vkr_video_session *sess = vkr_context_alloc_object(
      ctx, sizeof(*sess), VK_OBJECT_TYPE_VIDEO_SESSION_KHR, args->pVideoSession);
   if (!sess) {
      args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
      return;
   }

   /* Capture the DPB limits the guest asked for, so the decode path can range
    * check slots without re-querying the host per command. The host validates
    * these against its own capabilities on the create below, so a value it
    * accepts is one it will honour.
    */
   if (args->pCreateInfo) {
      sess->max_dpb_slots = args->pCreateInfo->maxDpbSlots;
      sess->max_active_references = args->pCreateInfo->maxActiveReferencePictures;
   }

   vn_replace_vkCreateVideoSessionKHR_args_handle(args);
   args->ret = vk->CreateVideoSessionKHR(args->device, args->pCreateInfo, NULL,
                                         &sess->base.handle.video_session);
   if (args->ret != VK_SUCCESS) {
      free(sess);
      return;
   }

   vkr_device_add_object(ctx, dev, &sess->base);
   ctx->video_session_count++;

   vkr_video_session_creates++;
   vkr_log("VIDEO-EVIDENCE session created (total=%" PRIu64 ")",
           vkr_video_session_creates);
}

static void
vkr_dispatch_vkDestroyVideoSessionKHR(struct vn_dispatch_context *dispatch,
                                      struct vn_command_vkDestroyVideoSessionKHR *args)
{
   struct vkr_context *ctx = dispatch->data;
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;
   struct vkr_video_session *sess = vkr_video_session_from_handle(args->videoSession);

   if (!sess)
      return;

   /* NO cascade to parameters objects.
    *
    * The spike reaped them here on the vkr_descriptor_pool precedent. That was
    * wrong: sessions and parameters are siblings owned by the device, and the
    * reap freed the renderer's record WITHOUT calling
    * vkDestroyVideoSessionParametersKHR, so the host object leaked and a
    * conformant guest destroying its parameters afterwards found nothing.
    */
   vn_replace_vkDestroyVideoSessionKHR_args_handle(args);
   vk->DestroyVideoSessionKHR(args->device, args->videoSession, NULL);

   vkr_device_remove_object(ctx, dev, &sess->base);
   if (ctx->video_session_count)
      ctx->video_session_count--;
}

static void
vkr_dispatch_vkGetVideoSessionMemoryRequirementsKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkGetVideoSessionMemoryRequirementsKHR *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   if (!vk->GetVideoSessionMemoryRequirementsKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   vn_replace_vkGetVideoSessionMemoryRequirementsKHR_args_handle(args);
   args->ret = vk->GetVideoSessionMemoryRequirementsKHR(
      args->device, args->videoSession, args->pMemoryRequirementsCount,
      args->pMemoryRequirements);
}

static void
vkr_dispatch_vkBindVideoSessionMemoryKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkBindVideoSessionMemoryKHR *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   if (!vk->BindVideoSessionMemoryKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   struct vkr_video_session *sess =
      vkr_video_session_from_handle(args->videoSession);

   vn_replace_vkBindVideoSessionMemoryKHR_args_handle(args);
   args->ret = vk->BindVideoSessionMemoryKHR(args->device, args->videoSession,
                                             args->bindSessionMemoryInfoCount,
                                             args->pBindSessionMemoryInfos);

   /* Record what was bound so liveness is checkable at destroy. Counted only
    * on success: a failed bind changes nothing host side.
    */
   if (args->ret == VK_SUCCESS && sess)
      sess->bound_memory_count += args->bindSessionMemoryInfoCount;
}

static void
vkr_dispatch_vkCreateVideoSessionParametersKHR(
   struct vn_dispatch_context *dispatch,
   struct vn_command_vkCreateVideoSessionParametersKHR *args)
{
   struct vkr_context *ctx = dispatch->data;
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   if (!vk->CreateVideoSessionParametersKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      if (args->pVideoSessionParameters)
         *args->pVideoSessionParameters = VK_NULL_HANDLE;
      return;
   }

   struct vkr_video_session_parameters *params =
      vkr_context_alloc_object(ctx, sizeof(*params),
                               VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR,
                               args->pVideoSessionParameters);
   if (!params) {
      args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
      return;
   }

   vn_replace_vkCreateVideoSessionParametersKHR_args_handle(args);
   args->ret = vk->CreateVideoSessionParametersKHR(
      args->device, args->pCreateInfo, NULL,
      &params->base.handle.video_session_parameters);
   if (args->ret != VK_SUCCESS) {
      free(params);
      return;
   }

   vkr_device_add_object(ctx, dev, &params->base);
}

static void
vkr_dispatch_vkUpdateVideoSessionParametersKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkUpdateVideoSessionParametersKHR *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   struct vkr_video_session_parameters *params =
      vkr_video_session_parameters_from_handle(args->videoSessionParameters);

   if (!vk->UpdateVideoSessionParametersKHR || !params) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   /* The guest value must be strictly GREATER than the stored one.
    *
    * Not "exactly one more": that would reject a conformant client whose
    * counter advances by more than one. Checking before forwarding keeps the
    * renderer and the host from disagreeing about which parameter sets exist.
    */
   if (!args->pUpdateInfo ||
       args->pUpdateInfo->updateSequenceCount <= params->update_sequence_count) {
      args->ret = VK_ERROR_VALIDATION_FAILED_EXT;
      return;
   }
   const uint32_t next = args->pUpdateInfo->updateSequenceCount;

   vn_replace_vkUpdateVideoSessionParametersKHR_args_handle(args);
   args->ret = vk->UpdateVideoSessionParametersKHR(
      args->device, args->videoSessionParameters, args->pUpdateInfo);

   /* Advance only on host success, so a rejected update leaves the two sides
    * agreeing rather than skipping a value the host never saw.
    */
   if (args->ret == VK_SUCCESS)
      params->update_sequence_count = next;
}

static void
vkr_dispatch_vkDestroyVideoSessionParametersKHR(
   struct vn_dispatch_context *dispatch,
   struct vn_command_vkDestroyVideoSessionParametersKHR *args)
{
   struct vkr_context *ctx = dispatch->data;
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;
   struct vkr_video_session_parameters *params =
      vkr_video_session_parameters_from_handle(args->videoSessionParameters);

   if (!params)
      return;

   vn_replace_vkDestroyVideoSessionParametersKHR_args_handle(args);
   vk->DestroyVideoSessionParametersKHR(args->device, args->videoSessionParameters,
                                        NULL);

   vkr_device_remove_object(ctx, dev, &params->base);
}

/* --- command buffer recording -------------------------------------------
 *
 * These four are pure recording: no reply, no return value, and the host
 * driver validates. Coding-scope tracking (decode outside Begin/End is
 * undefined behaviour per spec, not a guaranteed error return) is a hardened
 * implementation concern and is not attempted here.
 */

static void
vkr_dispatch_vkCmdBeginVideoCodingKHR(struct vn_dispatch_context *dispatch,
                                      struct vn_command_vkCmdBeginVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdBeginVideoCodingKHR)
      return;

   /* Nested Begin is rejected. The scope is a boolean rather than a depth
    * counter because the spec has no nesting to model -- treating a second
    * Begin as "depth 2" would invent semantics the driver does not implement.
    */
   if (cmd->in_video_coding_scope) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   /* Capture the session BEFORE handle replacement.
    *
    * Replacement rewrites pBeginInfo in place, and afterwards the field holds
    * a HOST handle -- which is not a key into the renderer's object table. The
    * same ordering trap cost a bug in the spike's parameters-create path.
    */
   struct vkr_video_session *sess = NULL;
   if (args->pBeginInfo && args->pBeginInfo->videoSession != VK_NULL_HANDLE)
      sess = vkr_video_session_from_handle(args->pBeginInfo->videoSession);
   if (!sess) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   vn_replace_vkCmdBeginVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdBeginVideoCodingKHR(args->commandBuffer, args->pBeginInfo);
   cmd->in_video_coding_scope = true;
   cmd->video_coding_session_id = sess->base.id;
}

static void
vkr_dispatch_vkCmdControlVideoCodingKHR(
   struct vn_dispatch_context *dispatch,
   struct vn_command_vkCmdControlVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdControlVideoCodingKHR)
      return;

   if (!cmd->in_video_coding_scope) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   vn_replace_vkCmdControlVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdControlVideoCodingKHR(args->commandBuffer,
                                                   args->pCodingControlInfo);
}

static void
vkr_dispatch_vkCmdDecodeVideoKHR(struct vn_dispatch_context *dispatch,
                                 struct vn_command_vkCmdDecodeVideoKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdDecodeVideoKHR)
      return;

   /* Outside a coding scope this is UNDEFINED per spec, not an error return,
    * so the driver is entitled to do anything at all with it.
    */
   if (!cmd->in_video_coding_scope) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   /* Resolve the session the scope was opened with, by id.
    *
    * Looking it up rather than holding a pointer is what makes a destroyed
    * session a clean rejection instead of a read of freed memory.
    */
   struct vkr_video_session *sess =
      vkr_context_get_object(dispatch->data, cmd->video_coding_session_id);
   if (!sess || sess->base.type != VK_OBJECT_TYPE_VIDEO_SESSION_KHR) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   if (!vkr_video_validate_decode_info(sess, args->pDecodeInfo)) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   vkr_video_count_decode();

   vn_replace_vkCmdDecodeVideoKHR_args_handle(args);
   cmd->device->proc_table.CmdDecodeVideoKHR(args->commandBuffer, args->pDecodeInfo);
}

static void
vkr_dispatch_vkCmdEndVideoCodingKHR(struct vn_dispatch_context *dispatch,
                                    struct vn_command_vkCmdEndVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdEndVideoCodingKHR)
      return;

   if (!cmd->in_video_coding_scope) {
      vkr_context_set_fatal(dispatch->data);
      return;
   }

   vn_replace_vkCmdEndVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdEndVideoCodingKHR(args->commandBuffer,
                                                args->pEndCodingInfo);
   cmd->in_video_coding_scope = false;
   cmd->video_coding_session_id = 0;
}

void
vkr_context_init_video_dispatch(struct vkr_context *ctx)
{
   struct vn_dispatch_context *dispatch = &ctx->dispatch;

   dispatch->dispatch_vkGetPhysicalDeviceVideoCapabilitiesKHR =
      vkr_dispatch_vkGetPhysicalDeviceVideoCapabilitiesKHR;
   dispatch->dispatch_vkGetPhysicalDeviceVideoFormatPropertiesKHR =
      vkr_dispatch_vkGetPhysicalDeviceVideoFormatPropertiesKHR;

   dispatch->dispatch_vkCreateVideoSessionKHR = vkr_dispatch_vkCreateVideoSessionKHR;
   dispatch->dispatch_vkDestroyVideoSessionKHR = vkr_dispatch_vkDestroyVideoSessionKHR;
   dispatch->dispatch_vkGetVideoSessionMemoryRequirementsKHR =
      vkr_dispatch_vkGetVideoSessionMemoryRequirementsKHR;
   dispatch->dispatch_vkBindVideoSessionMemoryKHR =
      vkr_dispatch_vkBindVideoSessionMemoryKHR;

   dispatch->dispatch_vkCreateVideoSessionParametersKHR =
      vkr_dispatch_vkCreateVideoSessionParametersKHR;
   dispatch->dispatch_vkUpdateVideoSessionParametersKHR =
      vkr_dispatch_vkUpdateVideoSessionParametersKHR;
   dispatch->dispatch_vkDestroyVideoSessionParametersKHR =
      vkr_dispatch_vkDestroyVideoSessionParametersKHR;

   dispatch->dispatch_vkCmdBeginVideoCodingKHR = vkr_dispatch_vkCmdBeginVideoCodingKHR;
   dispatch->dispatch_vkCmdControlVideoCodingKHR =
      vkr_dispatch_vkCmdControlVideoCodingKHR;
   dispatch->dispatch_vkCmdDecodeVideoKHR = vkr_dispatch_vkCmdDecodeVideoKHR;
   dispatch->dispatch_vkCmdEndVideoCodingKHR = vkr_dispatch_vkCmdEndVideoCodingKHR;
}
