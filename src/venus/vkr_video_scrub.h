/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_VIDEO_SCRUB_H
#define VKR_VIDEO_SCRUB_H

#include "vkr_common.h"

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

/* VkQueueFlagBits */
#define VKR_VIDEO_QUEUE_BITS (VK_QUEUE_VIDEO_DECODE_BIT_KHR)

/* VkFormatFeatureFlagBits */
#define VKR_VIDEO_FORMAT_FEATURE_BITS                                                    \
   (VK_FORMAT_FEATURE_VIDEO_DECODE_OUTPUT_BIT_KHR |                                      \
    VK_FORMAT_FEATURE_VIDEO_DECODE_DPB_BIT_KHR)

/* VkFormatFeatureFlagBits2 */
#define VKR_VIDEO_FORMAT_FEATURE_BITS2                                                   \
   (VK_FORMAT_FEATURE_2_VIDEO_DECODE_OUTPUT_BIT_KHR |                                    \
    VK_FORMAT_FEATURE_2_VIDEO_DECODE_DPB_BIT_KHR)

static inline bool
vkr_video_is_video_image_layout(VkImageLayout layout)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR:
   case VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR:
   case VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR:
      return true;
   default:
      return false;
   }
}

/* VkQueueFamilyProperties.queueFlags, on both queue-family query paths. */
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

/*
 * The pNext chain of VkQueueFamilyProperties2 carries two video capability
 * structs of its own. Scrubbing the base queueFlags alone does not close the
 * leak: the capability also travels here, which is why the pNext walk exists
 * rather than a single field clear.
 */
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
            ((VkQueueFamilyVideoPropertiesKHR *)pnext)->videoCodecOperations = 0;
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
               list->pDrmFormatModifierProperties[i].drmFormatModifierTilingFeatures &=
                  ~(VkFormatFeatureFlags)VKR_VIDEO_FORMAT_FEATURE_BITS;
            }
         }
         break;
      }
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: {
         VkDrmFormatModifierPropertiesList2EXT *list =
            (VkDrmFormatModifierPropertiesList2EXT *)pnext;
         if (list->pDrmFormatModifierProperties) {
            for (uint32_t i = 0; i < list->drmFormatModifierCount; i++) {
               list->pDrmFormatModifierProperties[i].drmFormatModifierTilingFeatures &=
                  ~(VkFormatFeatureFlags2)VKR_VIDEO_FORMAT_FEATURE_BITS2;
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
      if (!vkr_video_is_video_image_layout(layouts[i]))
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

#endif /* VKR_VIDEO_SCRUB_H */
