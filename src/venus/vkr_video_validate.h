/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_VIDEO_VALIDATE_H
#define VKR_VIDEO_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

#include "vkr_common.h"
#include "vkr_video.h"

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
static inline bool
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
static inline bool
vkr_video_slot_index_is_ignored(int32_t slot_index)
{
   return slot_index < 0;
}

static inline bool
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

static inline bool
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

#endif /* VKR_VIDEO_VALIDATE_H */
