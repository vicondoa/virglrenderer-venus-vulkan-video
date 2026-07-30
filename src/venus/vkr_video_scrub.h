/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_VIDEO_SCRUB_H
#define VKR_VIDEO_SCRUB_H

#include "vkr_common.h"

/* Video masks and layout predicate are GENERATED; see vkr_video_reject.h. */
#include "vkr_video_reject.h"

/*
 * Remove video capability from guest-visible replies while video is
 * unadvertised.
 *
 * The renderer does not advertise VK_KHR_video_*, does not enable it on the
 * guest's device, dispatches none of its commands and masks it out of the
 * capset. None of that stops the host from reporting video capability through
 * commands that predate video and are dispatched normally: the queue-family
 * queries report VK_QUEUE_VIDEO_DECODE_BIT_KHR in the base queueFlags, the
 * format queries report VK_FORMAT_FEATURE*_VIDEO_DECODE_* bits, and the
 * property queries report video image layouts in the host-copy layout lists.
 *
 * A guest that sees those has been told the device can decode video, by a
 * renderer that cannot. Mesa's Venus driver already strips the format-feature
 * bits guest-side; this is the same job on the side that actually knows
 * whether the renderer supports the extension.
 *
 * Every site handled here is derived, not chosen: the lab's
 * tests/video-site-manifest-golden.txt lists each (struct, member, direction,
 * values, command paths), and tests/video-enforcement-gate.sh reports how many
 * of those sites are enforced. Sites are added by consulting that manifest, so
 * a new outbound member cannot be missed by not having been thought of.
 *
 * Remove this when video is genuinely supported end to end.
 */

/* Queue capability.
 *
 * The generated VKR_VIDEO_QUEUE_BITS is already exactly the UNSUPPORTED video
 * queue bits: the generator derives its masks by intersecting vk.xml against
 * SUPPORTED_VIDEO_EXTENSIONS, so a decode bit is not in it. Scrubbing the
 * whole mask therefore removes encode and preserves decode with no extra
 * logic here.
 *
 * An earlier version of this file carried a hand-written
 * VKR_VIDEO_SUPPORTED_QUEUE_BITS and subtracted it. That was dead code --
 * subtracting the decode bit from a mask that no longer contained it -- and
 * worse, it was a SECOND hand-written set deciding the same question the
 * generator already answers. It was found by a mutation that should have
 * fired and did not: forcing the supported set to zero changed nothing,
 * because the subtraction had no effect either way.
 */
static inline void
vkr_video_scrub_queue_family_properties(VkQueueFamilyProperties *props)
{
   props->queueFlags &= ~(VkQueueFlags)VKR_VIDEO_QUEUE_BITS;
}

static inline void
vkr_video_scrub_queue_family_properties_array(VkQueueFamilyProperties *props,
                                              uint32_t count)
{
   if (!props)
      return;
   for (uint32_t i = 0; i < count; i++)
      vkr_video_scrub_queue_family_properties(&props[i]);
}

static inline void
vkr_video_scrub_queue_family_properties2_array(VkQueueFamilyProperties2 *props,
                                               uint32_t count)
{
   if (!props)
      return;

   for (uint32_t i = 0; i < count; i++) {
      vkr_video_scrub_queue_family_properties(&props[i].queueFamilyProperties);

      for (VkBaseOutStructure *pnext = props[i].pNext; pnext; pnext = pnext->pNext) {
         switch (pnext->sType) {
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR:
            /* Report only the codec operations the renderer can carry.
             * Zeroing this outright would leave the decode queue bit set with
             * no codec named, which FFmpeg reads as "a video queue that
             * decodes nothing" -- a video-capable device it cannot use.
             */
            /* Hand-written, unlike the masks above, because the generator
             * emits no per-codec-operation set today. That makes it the one
             * remaining hand-written video set in this file, which is worth
             * knowing: it is the thing to derive next.
             */
            ((VkQueueFamilyVideoPropertiesKHR *)pnext)->videoCodecOperations &=
               (VkVideoCodecOperationFlagsKHR)
                  VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
            break;
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR:
            ((VkQueueFamilyQueryResultStatusPropertiesKHR *)pnext)
               ->queryResultStatusSupport = VK_FALSE;
            break;
         default:
            break;
         }
      }
   }
}

/* Named element helpers so the scrubbed struct type appears in the source.
 * The list walk only ever named the LIST type, which left no record that the
 * element type is the thing being scrubbed.
 */
static inline void
vkr_video_scrub_VkDrmFormatModifierPropertiesEXT(VkDrmFormatModifierPropertiesEXT *props)
{
   props->drmFormatModifierTilingFeatures &=
      ~(VkFormatFeatureFlags)VKR_VIDEO_FORMAT_FEATURE_BITS;
}

static inline void
vkr_video_scrub_VkDrmFormatModifierProperties2EXT(VkDrmFormatModifierProperties2EXT *props)
{
   props->drmFormatModifierTilingFeatures &=
      ~(VkFormatFeatureFlags2)VKR_VIDEO_FORMAT_FEATURE_BITS2;
}

static inline void
vkr_video_scrub_format_properties(VkFormatProperties *props)
{
   const VkFormatFeatureFlags mask = ~(VkFormatFeatureFlags)VKR_VIDEO_FORMAT_FEATURE_BITS;
   props->linearTilingFeatures &= mask;
   props->optimalTilingFeatures &= mask;
   props->bufferFeatures &= mask;
}

static inline void
vkr_video_scrub_format_properties2(VkFormatProperties2 *props)
{
   if (!props)
      return;

   vkr_video_scrub_format_properties(&props->formatProperties);

   for (VkBaseOutStructure *pnext = props->pNext; pnext; pnext = pnext->pNext) {
      switch (pnext->sType) {
      case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3: {
         VkFormatProperties3 *p3 = (VkFormatProperties3 *)pnext;
         const VkFormatFeatureFlags2 m =
            ~(VkFormatFeatureFlags2)VKR_VIDEO_FORMAT_FEATURE_BITS2;
         p3->linearTilingFeatures &= m;
         p3->optimalTilingFeatures &= m;
         p3->bufferFeatures &= m;
         break;
      }
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: {
         VkDrmFormatModifierPropertiesListEXT *list =
            (VkDrmFormatModifierPropertiesListEXT *)pnext;
         if (list->pDrmFormatModifierProperties) {
            for (uint32_t i = 0; i < list->drmFormatModifierCount; i++) {
               vkr_video_scrub_VkDrmFormatModifierPropertiesEXT(
                  &list->pDrmFormatModifierProperties[i]);
            }
         }
         break;
      }
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: {
         VkDrmFormatModifierPropertiesList2EXT *list =
            (VkDrmFormatModifierPropertiesList2EXT *)pnext;
         if (list->pDrmFormatModifierProperties) {
            for (uint32_t i = 0; i < list->drmFormatModifierCount; i++) {
               vkr_video_scrub_VkDrmFormatModifierProperties2EXT(
                  &list->pDrmFormatModifierProperties[i]);
            }
         }
         break;
      }
      default:
         break;
      }
   }
}

/*
 * Host-copy layout lists report which image layouts support host copies, and
 * the video layouts appear there once the host driver supports video. The
 * count is an out parameter, so entries are removed by compaction rather than
 * zeroed: a zeroed entry would read as VK_IMAGE_LAYOUT_UNDEFINED, which is a
 * different and wrong answer.
 */
static inline void
vkr_video_scrub_image_layout_list(VkImageLayout *layouts, uint32_t *count)
{
   if (!count)
      return;
   if (!layouts) {
      /* Capacity query. The reported count must match what the filtered list
       * will actually contain, or the guest allocates for entries it never
       * receives and reads uninitialized memory for the difference.
       *
       * The host count cannot be filtered without the list, so it is left
       * alone: over-reporting capacity is safe, under-reporting is not.
       */
      return;
   }

   uint32_t kept = 0;
   for (uint32_t i = 0; i < *count; i++) {
      if (!vkr_video_is_video_layout(layouts[i]))
         layouts[kept++] = layouts[i];
   }
   *count = kept;
}

static inline void
vkr_video_scrub_physical_device_properties2(VkPhysicalDeviceProperties2 *props)
{
   if (!props)
      return;

   for (VkBaseOutStructure *pnext = props->pNext; pnext; pnext = pnext->pNext) {
      switch (pnext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES: {
         VkPhysicalDeviceHostImageCopyProperties *p =
            (VkPhysicalDeviceHostImageCopyProperties *)pnext;
         vkr_video_scrub_image_layout_list(p->pCopySrcLayouts, &p->copySrcLayoutCount);
         vkr_video_scrub_image_layout_list(p->pCopyDstLayouts, &p->copyDstLayoutCount);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES: {
         VkPhysicalDeviceVulkan14Properties *p =
            (VkPhysicalDeviceVulkan14Properties *)pnext;
         vkr_video_scrub_image_layout_list(p->pCopySrcLayouts, &p->copySrcLayoutCount);
         vkr_video_scrub_image_layout_list(p->pCopyDstLayouts, &p->copyDstLayoutCount);
         break;
      }
      default:
         break;
      }
   }
}

/* Outbound scrub for the video format query reply.
 *
 * The format allowlist in vkr_video_validate.h decides WHICH format rows
 * survive. It says nothing about the flag members on a surviving row, and the
 * host is entitled to report a format that is both decode- and encode-capable
 * with the encode bits set. Forwarding those tells the guest it may create
 * encode images through a renderer with no encode implementation.
 *
 * So the row filter and this scrub are two obligations, not one. Treating the
 * filter as covering both is the split-obligation mistake that produced false
 * passes in the enforcement and reply-hygiene gates.
 */
static inline void
vkr_video_scrub_video_format_properties(VkVideoFormatPropertiesKHR *props)
{
   const VkImageUsageFlags allowed_usage =
      (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
      (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
      (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR |
      (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      (VkImageUsageFlags)VK_IMAGE_USAGE_SAMPLED_BIT;

   props->imageUsageFlags &= allowed_usage;
   props->imageCreateFlags &=
      ~(VkImageCreateFlags)VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
}

static inline void
vkr_video_scrub_video_format_properties_array(VkVideoFormatPropertiesKHR *props,
                                              uint32_t count)
{
   if (!props)
      return;
   for (uint32_t i = 0; i < count; i++)
      vkr_video_scrub_video_format_properties(&props[i]);
}

#endif /* VKR_VIDEO_SCRUB_H */
