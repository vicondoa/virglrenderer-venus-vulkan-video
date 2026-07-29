/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vkr_render_pass.h"

#include "vkr_render_pass_gen.h"
#include "vkr_video_reject.h"

/* Attachments carry VkImageLayout, and three of its values are video decode
 * layouts. Rejecting them here keeps a guest from naming a video layout on an
 * ordinary render pass, which is reachable without any video command being
 * dispatched.
 */
static bool
vkr_render_pass_has_video_layout(const VkRenderPassCreateInfo *info)
{
   if (!info)
      return false;
   for (uint32_t i = 0; i < info->attachmentCount; i++) {
      if (vkr_video_reject_VkAttachmentDescription(&info->pAttachments[i]))
         return true;
   }
   for (uint32_t i = 0; i < info->subpassCount; i++) {
      const VkSubpassDescription *sub = &info->pSubpasses[i];
      for (uint32_t j = 0; j < sub->inputAttachmentCount; j++) {
         if (vkr_video_reject_VkAttachmentReference(&sub->pInputAttachments[j]))
            return true;
      }
      for (uint32_t j = 0; j < sub->colorAttachmentCount; j++) {
         if (vkr_video_reject_VkAttachmentReference(&sub->pColorAttachments[j]))
            return true;
         if (sub->pResolveAttachments &&
             vkr_video_reject_VkAttachmentReference(&sub->pResolveAttachments[j]))
            return true;
      }
      if (sub->pDepthStencilAttachment &&
          vkr_video_reject_VkAttachmentReference(sub->pDepthStencilAttachment))
         return true;
   }
   return false;
}

static void
vkr_dispatch_vkCreateRenderPass(struct vn_dispatch_context *dispatch,
                                struct vn_command_vkCreateRenderPass *args)
{
   if (vkr_render_pass_has_video_layout(args->pCreateInfo)) {
      args->ret = VK_ERROR_FEATURE_NOT_PRESENT;
      return;
   }

   vkr_render_pass_create_and_add(dispatch->data, args);
}

static bool
vkr_render_pass2_has_video_layout(const VkRenderPassCreateInfo2 *info)
{
   if (!info)
      return false;
   for (uint32_t i = 0; i < info->attachmentCount; i++) {
      if (vkr_video_reject_VkAttachmentDescription2(&info->pAttachments[i]) ||
          vkr_video_reject_pnext(info->pAttachments[i].pNext))
         return true;
   }
   for (uint32_t i = 0; i < info->subpassCount; i++) {
      const VkSubpassDescription2 *sub = &info->pSubpasses[i];
      for (uint32_t j = 0; j < sub->inputAttachmentCount; j++) {
         if (vkr_video_reject_VkAttachmentReference2(&sub->pInputAttachments[j]))
            return true;
      }
      for (uint32_t j = 0; j < sub->colorAttachmentCount; j++) {
         if (vkr_video_reject_VkAttachmentReference2(&sub->pColorAttachments[j]))
            return true;
         if (sub->pResolveAttachments &&
             vkr_video_reject_VkAttachmentReference2(&sub->pResolveAttachments[j]))
            return true;
      }
      if (sub->pDepthStencilAttachment &&
          vkr_video_reject_VkAttachmentReference2(sub->pDepthStencilAttachment))
         return true;
   }
   return false;
}

static void
vkr_dispatch_vkCreateRenderPass2(struct vn_dispatch_context *dispatch,
                                 struct vn_command_vkCreateRenderPass2 *args)
{
   if (vkr_render_pass2_has_video_layout(args->pCreateInfo)) {
      args->ret = VK_ERROR_FEATURE_NOT_PRESENT;
      return;
   }

   struct vkr_context *ctx = dispatch->data;
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   struct vkr_render_pass *pass = vkr_context_alloc_object(
      ctx, sizeof(*pass), VK_OBJECT_TYPE_RENDER_PASS, args->pRenderPass);
   if (!pass) {
      args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
      return;
   }

   vn_replace_vkCreateRenderPass2_args_handle(args);
   args->ret = vk->CreateRenderPass2(args->device, args->pCreateInfo, NULL,
                                     &pass->base.handle.render_pass);
   if (args->ret != VK_SUCCESS) {
      free(pass);
      return;
   }

   vkr_device_add_object(ctx, dev, &pass->base);
}

static void
vkr_dispatch_vkDestroyRenderPass(struct vn_dispatch_context *dispatch,
                                 struct vn_command_vkDestroyRenderPass *args)
{
   vkr_render_pass_destroy_and_remove(dispatch->data, args);
}

static void
vkr_dispatch_vkGetRenderAreaGranularity(UNUSED struct vn_dispatch_context *dispatch,
                                        struct vn_command_vkGetRenderAreaGranularity *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   vn_replace_vkGetRenderAreaGranularity_args_handle(args);
   vk->GetRenderAreaGranularity(args->device, args->renderPass, args->pGranularity);
}

static void
vkr_dispatch_vkGetRenderingAreaGranularity(
   UNUSED struct vn_dispatch_context *dispatch,
   struct vn_command_vkGetRenderingAreaGranularity *args)
{
   struct vkr_device *dev = vkr_device_from_handle(args->device);
   struct vn_device_proc_table *vk = &dev->proc_table;

   vn_replace_vkGetRenderingAreaGranularity_args_handle(args);
   vk->GetRenderingAreaGranularity(args->device, args->pRenderingAreaInfo,
                                   args->pGranularity);
}

static void
vkr_dispatch_vkCreateFramebuffer(struct vn_dispatch_context *dispatch,
                                 struct vn_command_vkCreateFramebuffer *args)
{
   vkr_framebuffer_create_and_add(dispatch->data, args);
}

static void
vkr_dispatch_vkDestroyFramebuffer(struct vn_dispatch_context *dispatch,
                                  struct vn_command_vkDestroyFramebuffer *args)
{
   vkr_framebuffer_destroy_and_remove(dispatch->data, args);
}

void
vkr_context_init_render_pass_dispatch(struct vkr_context *ctx)
{
   struct vn_dispatch_context *dispatch = &ctx->dispatch;

   dispatch->dispatch_vkCreateRenderPass = vkr_dispatch_vkCreateRenderPass;
   dispatch->dispatch_vkCreateRenderPass2 = vkr_dispatch_vkCreateRenderPass2;
   dispatch->dispatch_vkDestroyRenderPass = vkr_dispatch_vkDestroyRenderPass;
   dispatch->dispatch_vkGetRenderAreaGranularity =
      vkr_dispatch_vkGetRenderAreaGranularity;
   dispatch->dispatch_vkGetRenderingAreaGranularity =
      vkr_dispatch_vkGetRenderingAreaGranularity;
}

void
vkr_context_init_framebuffer_dispatch(struct vkr_context *ctx)
{
   struct vn_dispatch_context *dispatch = &ctx->dispatch;

   dispatch->dispatch_vkCreateFramebuffer = vkr_dispatch_vkCreateFramebuffer;
   dispatch->dispatch_vkDestroyFramebuffer = vkr_dispatch_vkDestroyFramebuffer;
}
