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

   struct vkr_video_session *sess = vkr_context_alloc_object(
      ctx, sizeof(*sess), VK_OBJECT_TYPE_VIDEO_SESSION_KHR, args->pVideoSession);
   if (!sess) {
      args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
      return;
   }

   list_inithead(&sess->parameters);

   vn_replace_vkCreateVideoSessionKHR_args_handle(args);
   args->ret = vk->CreateVideoSessionKHR(args->device, args->pCreateInfo, NULL,
                                         &sess->base.handle.video_session);
   if (args->ret != VK_SUCCESS) {
      free(sess);
      return;
   }

   vkr_device_add_object(ctx, dev, &sess->base);

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

   /* Destroying a session implicitly destroys its parameters objects host
    * side. Reap the renderer's records first, or the object table keeps
    * entries naming handles the driver has already freed -- and a later guest
    * reference to one of them would be forwarded as a live handle.
    */
   vkr_context_remove_objects(ctx, &sess->parameters);

   vn_replace_vkDestroyVideoSessionKHR_args_handle(args);
   vk->DestroyVideoSessionKHR(args->device, args->videoSession, NULL);

   vkr_device_remove_object(ctx, dev, &sess->base);
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

   vn_replace_vkBindVideoSessionMemoryKHR_args_handle(args);
   args->ret = vk->BindVideoSessionMemoryKHR(args->device, args->videoSession,
                                             args->bindSessionMemoryInfoCount,
                                             args->pBindSessionMemoryInfos);
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

   /* Resolve the owning session BEFORE handle replacement rewrites the create
    * info in place -- afterwards the field holds a host handle, which is not a
    * key into the renderer's object table.
    */
   struct vkr_video_session *sess = NULL;
   if (args->pCreateInfo && args->pCreateInfo->videoSession != VK_NULL_HANDLE)
      sess = vkr_video_session_from_handle(args->pCreateInfo->videoSession);

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

   /* Re-home onto the session so destroying the session reaps this too.
    * vkr_device_add_object put it on the device's tracking list; moving it
    * keeps exactly one owner, so the cascade cannot double-free.
    */
   if (sess) {
      list_del(&params->base.track_head);
      list_add(&params->base.track_head, &sess->parameters);
   }
}

static void
vkr_dispatch_vkUpdateVideoSessionParametersKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkUpdateVideoSessionParametersKHR *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   if (!vk->UpdateVideoSessionParametersKHR) {
      args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
      return;
   }

   vn_replace_vkUpdateVideoSessionParametersKHR_args_handle(args);
   args->ret = vk->UpdateVideoSessionParametersKHR(
      args->device, args->videoSessionParameters, args->pUpdateInfo);
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
vkr_dispatch_vkCmdBeginVideoCodingKHR(UNUSED struct vn_dispatch_context *dispatch,
                                      struct vn_command_vkCmdBeginVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdBeginVideoCodingKHR)
      return;

   vn_replace_vkCmdBeginVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdBeginVideoCodingKHR(args->commandBuffer, args->pBeginInfo);
}

static void
vkr_dispatch_vkCmdControlVideoCodingKHR(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkCmdControlVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdControlVideoCodingKHR)
      return;

   vn_replace_vkCmdControlVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdControlVideoCodingKHR(args->commandBuffer,
                                                   args->pCodingControlInfo);
}

static void
vkr_dispatch_vkCmdDecodeVideoKHR(UNUSED struct vn_dispatch_context *dispatch,
                                 struct vn_command_vkCmdDecodeVideoKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdDecodeVideoKHR)
      return;

   vkr_video_count_decode();

   vn_replace_vkCmdDecodeVideoKHR_args_handle(args);
   cmd->device->proc_table.CmdDecodeVideoKHR(args->commandBuffer, args->pDecodeInfo);
}

static void
vkr_dispatch_vkCmdEndVideoCodingKHR(UNUSED struct vn_dispatch_context *dispatch,
                                    struct vn_command_vkCmdEndVideoCodingKHR *args)
{
   struct vkr_command_buffer *cmd = vkr_command_buffer_from_handle(args->commandBuffer);
   if (!cmd || !cmd->device->proc_table.CmdEndVideoCodingKHR)
      return;

   vn_replace_vkCmdEndVideoCodingKHR_args_handle(args);
   cmd->device->proc_table.CmdEndVideoCodingKHR(args->commandBuffer,
                                                args->pEndCodingInfo);
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
