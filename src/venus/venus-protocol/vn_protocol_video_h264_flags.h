/*
 * Copyright 2026 the Venus Vulkan Video lab contributors
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 * Explicit bit packing for the StdVideo H.264 *Flags bitfield structs.
 *
 * WHY THIS IS NOT GENERATED
 *
 * The generator's scalar helpers take pointers:
 *
 *     static inline void vn_encode_uint32_t(struct vn_cs_encoder *enc,
 *                                           const uint32_t *val);
 *
 * and taking the address of a bitfield is illegal C, so the flags cannot be
 * described as ordinary members in XML. Describing a *Flags struct as one
 * opaque uint32_t and copying it would compile, but C leaves bitfield
 * allocation order and padding implementation-defined -- guest and host are not
 * guaranteed to agree, and a disagreement would corrupt decode parameters
 * silently rather than failing.
 *
 * So each flags struct crosses the wire as a single uint32_t whose layout is
 * fixed HERE by explicit shifts over named fields. Each side then lets its own
 * compiler lay out the bitfield however it likes; only these shift constants
 * are shared.
 *
 * THE SHIFT ASSIGNMENTS ARE WIRE CONTRACT.
 *
 * They are append-only, exactly like the VkCommandTypeEXT ids. Renumbering or
 * reordering a bit silently changes the meaning of a decode parameter for every
 * peer built against a different revision. Add new bits at the next free
 * position; never reuse or reorder.
 */

#ifndef VN_PROTOCOL_VIDEO_H264_FLAGS_H
#define VN_PROTOCOL_VIDEO_H264_FLAGS_H

#include <stdint.h>

#include "vk_video/vulkan_video_codec_h264std.h"
#include "vk_video/vulkan_video_codec_h264std_decode.h"

/* StdVideoDecodeH264PictureInfoFlags — bits 0..5, append-only. */
#define VN_H264_PIC_FLAG_field_pic_flag            0u
#define VN_H264_PIC_FLAG_is_intra                  1u
#define VN_H264_PIC_FLAG_IdrPicFlag                2u
#define VN_H264_PIC_FLAG_bottom_field_flag         3u
#define VN_H264_PIC_FLAG_is_reference              4u
#define VN_H264_PIC_FLAG_complementary_field_pair  5u
#define VN_H264_PIC_FLAG_VALID_MASK                0x3fu

/* StdVideoDecodeH264ReferenceInfoFlags — bits 0..3, append-only. */
#define VN_H264_REF_FLAG_top_field_flag               0u
#define VN_H264_REF_FLAG_bottom_field_flag            1u
#define VN_H264_REF_FLAG_used_for_long_term_reference 2u
#define VN_H264_REF_FLAG_is_non_existing              3u
#define VN_H264_REF_FLAG_VALID_MASK                   0x0fu

static inline uint32_t
vn_pack_StdVideoDecodeH264PictureInfoFlags(
   const StdVideoDecodeH264PictureInfoFlags *f)
{
   return ((uint32_t)(f->field_pic_flag           & 1u) << VN_H264_PIC_FLAG_field_pic_flag) |
          ((uint32_t)(f->is_intra                 & 1u) << VN_H264_PIC_FLAG_is_intra) |
          ((uint32_t)(f->IdrPicFlag               & 1u) << VN_H264_PIC_FLAG_IdrPicFlag) |
          ((uint32_t)(f->bottom_field_flag        & 1u) << VN_H264_PIC_FLAG_bottom_field_flag) |
          ((uint32_t)(f->is_reference             & 1u) << VN_H264_PIC_FLAG_is_reference) |
          ((uint32_t)(f->complementary_field_pair & 1u) << VN_H264_PIC_FLAG_complementary_field_pair);
}

/*
 * Returns false if the guest set any bit outside the defined range.
 *
 * Unknown bits are REJECTED rather than masked away: a peer setting them is
 * either a newer revision we cannot correctly interpret, or a malformed
 * stream. Silently clearing them would let a decode proceed with parameters
 * that do not mean what the sender intended.
 */
static inline bool
vn_unpack_StdVideoDecodeH264PictureInfoFlags(
   uint32_t bits, StdVideoDecodeH264PictureInfoFlags *f)
{
   if (bits & ~VN_H264_PIC_FLAG_VALID_MASK)
      return false;

   f->field_pic_flag           = (bits >> VN_H264_PIC_FLAG_field_pic_flag) & 1u;
   f->is_intra                 = (bits >> VN_H264_PIC_FLAG_is_intra) & 1u;
   f->IdrPicFlag               = (bits >> VN_H264_PIC_FLAG_IdrPicFlag) & 1u;
   f->bottom_field_flag        = (bits >> VN_H264_PIC_FLAG_bottom_field_flag) & 1u;
   f->is_reference             = (bits >> VN_H264_PIC_FLAG_is_reference) & 1u;
   f->complementary_field_pair = (bits >> VN_H264_PIC_FLAG_complementary_field_pair) & 1u;
   return true;
}

static inline uint32_t
vn_pack_StdVideoDecodeH264ReferenceInfoFlags(
   const StdVideoDecodeH264ReferenceInfoFlags *f)
{
   return ((uint32_t)(f->top_field_flag               & 1u) << VN_H264_REF_FLAG_top_field_flag) |
          ((uint32_t)(f->bottom_field_flag            & 1u) << VN_H264_REF_FLAG_bottom_field_flag) |
          ((uint32_t)(f->used_for_long_term_reference & 1u) << VN_H264_REF_FLAG_used_for_long_term_reference) |
          ((uint32_t)(f->is_non_existing              & 1u) << VN_H264_REF_FLAG_is_non_existing);
}

static inline bool
vn_unpack_StdVideoDecodeH264ReferenceInfoFlags(
   uint32_t bits, StdVideoDecodeH264ReferenceInfoFlags *f)
{
   if (bits & ~VN_H264_REF_FLAG_VALID_MASK)
      return false;

   f->top_field_flag               = (bits >> VN_H264_REF_FLAG_top_field_flag) & 1u;
   f->bottom_field_flag            = (bits >> VN_H264_REF_FLAG_bottom_field_flag) & 1u;
   f->used_for_long_term_reference = (bits >> VN_H264_REF_FLAG_used_for_long_term_reference) & 1u;
   f->is_non_existing              = (bits >> VN_H264_REF_FLAG_is_non_existing) & 1u;
   return true;
}

/* StdVideoH264SpsFlags — bits 0..15, append-only. */
#define VN_H264_SPS_FLAG_VALID_MASK 0xffffu

#define VN_H264_SPS_BITS(X)                                     \
   X(constraint_set0_flag,                    0)                \
   X(constraint_set1_flag,                    1)                \
   X(constraint_set2_flag,                    2)                \
   X(constraint_set3_flag,                    3)                \
   X(constraint_set4_flag,                    4)                \
   X(constraint_set5_flag,                    5)                \
   X(direct_8x8_inference_flag,               6)                \
   X(mb_adaptive_frame_field_flag,            7)                \
   X(frame_mbs_only_flag,                     8)                \
   X(delta_pic_order_always_zero_flag,        9)                \
   X(separate_colour_plane_flag,             10)                \
   X(gaps_in_frame_num_value_allowed_flag,   11)                \
   X(qpprime_y_zero_transform_bypass_flag,   12)                \
   X(frame_cropping_flag,                    13)                \
   X(seq_scaling_matrix_present_flag,        14)                \
   X(vui_parameters_present_flag,            15)

/* StdVideoH264SpsVuiFlags — bits 0..11, append-only. */
#define VN_H264_VUI_FLAG_VALID_MASK 0x0fffu

#define VN_H264_VUI_BITS(X)                                     \
   X(aspect_ratio_info_present_flag,          0)                \
   X(overscan_info_present_flag,              1)                \
   X(overscan_appropriate_flag,               2)                \
   X(video_signal_type_present_flag,          3)                \
   X(video_full_range_flag,                   4)                \
   X(color_description_present_flag,          5)                \
   X(chroma_loc_info_present_flag,            6)                \
   X(timing_info_present_flag,                7)                \
   X(fixed_frame_rate_flag,                   8)                \
   X(bitstream_restriction_flag,              9)                \
   X(nal_hrd_parameters_present_flag,        10)                \
   X(vcl_hrd_parameters_present_flag,        11)

/* StdVideoH264PpsFlags — bits 0..7, append-only. */
#define VN_H264_PPS_FLAG_VALID_MASK 0x00ffu

#define VN_H264_PPS_BITS(X)                                     \
   X(transform_8x8_mode_flag,                 0)                \
   X(redundant_pic_cnt_present_flag,          1)                \
   X(constrained_intra_pred_flag,             2)                \
   X(deblocking_filter_control_present_flag,  3)                \
   X(weighted_pred_flag,                      4)                \
   X(bottom_field_pic_order_in_frame_present_flag, 5)           \
   X(entropy_coding_mode_flag,                6)                \
   X(pic_scaling_matrix_present_flag,         7)

/*
 * The pack/unpack pairs below are generated from the X-macro bit lists above,
 * so a bit can only be added by editing one list and cannot drift between the
 * two directions.
 */
#define VN_H264_DEFINE_FLAG_HELPERS(Type, PREFIX, BITS)                  \
   static inline uint32_t vn_pack_##Type(const Type *f)                  \
   {                                                                     \
      uint32_t bits = 0;                                                 \
      BITS(VN_H264_PACK_ONE)                                             \
      return bits;                                                       \
   }                                                                     \
   static inline bool vn_unpack_##Type(uint32_t bits, Type *f)           \
   {                                                                     \
      if (bits & ~PREFIX##_VALID_MASK)                                   \
         return false;                                                   \
      BITS(VN_H264_UNPACK_ONE)                                           \
      return true;                                                       \
   }

#define VN_H264_PACK_ONE(name, bit)   bits |= (uint32_t)(f->name & 1u) << (bit);
#define VN_H264_UNPACK_ONE(name, bit) f->name = (bits >> (bit)) & 1u;

VN_H264_DEFINE_FLAG_HELPERS(StdVideoH264SpsFlags,    VN_H264_SPS_FLAG, VN_H264_SPS_BITS)
VN_H264_DEFINE_FLAG_HELPERS(StdVideoH264SpsVuiFlags, VN_H264_VUI_FLAG, VN_H264_VUI_BITS)
VN_H264_DEFINE_FLAG_HELPERS(StdVideoH264PpsFlags,    VN_H264_PPS_FLAG, VN_H264_PPS_BITS)

/*
 * Serialization trio for a bitfield flags struct.
 *
 * Defined as a macro so the Venus template only has to name the types, and so
 * the encode and decode directions cannot drift apart. Decoding marks the
 * stream fatal on undefined bits rather than masking them: a peer setting them
 * is either a newer revision we cannot interpret or a malformed stream.
 */
#define VN_H264_DEFINE_FLAG_SERIALIZERS(Type)                                 \
   static inline size_t vn_sizeof_##Type(const Type *val)                     \
   {                                                                          \
      return vn_sizeof_uint32_t(&(uint32_t){ vn_pack_##Type(val) });          \
   }                                                                          \
   static inline void                                                         \
   vn_encode_##Type(struct vn_cs_encoder *enc, const Type *val)               \
   {                                                                          \
      const uint32_t tmp = vn_pack_##Type(val);                               \
      vn_encode_uint32_t(enc, &tmp);                                          \
   }                                                                          \
   static inline void                                                         \
   vn_decode_##Type(struct vn_cs_decoder *dec, Type *val)                     \
   {                                                                          \
      uint32_t tmp;                                                           \
      vn_decode_uint32_t(dec, &tmp);                                          \
      if (!vn_unpack_##Type(tmp, val))                                        \
         vn_cs_decoder_set_fatal(dec);                                        \
   }                                                                     \
   /* Renderer-side variants. _temp needs no allocation because the packed   \
    * form is a flat uint32_t, and _handle is a no-op because a flags struct \
    * contains no Vulkan handles to translate. */                            \
   static inline void                                                        \
   vn_decode_##Type##_temp(struct vn_cs_decoder *dec, Type *val)             \
   {                                                                         \
      vn_decode_##Type(dec, val);                                            \
   }                                                                         \
   static inline void vn_replace_##Type##_handle(Type *val)                  \
   {                                                                         \
      (void)val;                                                             \
   }

/*
 * Serialization trio for a StdVideo enum.
 *
 * vk.xml declares these by name only, so the generator emits calls to
 * vn_{sizeof,encode,decode}_<Enum> without ever defining them. They are plain C
 * enums, but their underlying type is implementation-defined, so they are not
 * interchangeable with int32_t at the pointer level -- passing &val->field to
 * vn_encode_int32_t would be a type error.
 *
 * Encoding therefore goes through an int32_t temporary, exactly as the existing
 * vn_custom_size_t helper does for size_t. Decoding rejects values that do not
 * survive the round trip, which catches a truncating or out-of-range value from
 * a malformed or newer peer instead of quietly storing it.
 */
#define VN_H264_DEFINE_ENUM_SERIALIZERS(Enum)                                 \
   static inline size_t vn_sizeof_##Enum(const Enum *val)                     \
   {                                                                          \
      return vn_sizeof_int32_t(&(int32_t){ (int32_t)*val });                  \
   }                                                                          \
   static inline void                                                         \
   vn_encode_##Enum(struct vn_cs_encoder *enc, const Enum *val)               \
   {                                                                          \
      const int32_t tmp = (int32_t)*val;                                      \
      vn_encode_int32_t(enc, &tmp);                                           \
   }                                                                          \
   static inline void                                                         \
   vn_decode_##Enum(struct vn_cs_decoder *dec, Enum *val)                     \
   {                                                                          \
      int32_t tmp;                                                            \
      vn_decode_int32_t(dec, &tmp);                                           \
      *val = (Enum)tmp;                                                       \
      if ((int32_t)*val != tmp)                                               \
         vn_cs_decoder_set_fatal(dec);                                        \
   }                                                                          \
   /* Renderer-side variants. _temp needs no allocation because an enum is a  \
    * scalar, and _handle is a no-op because there is no Vulkan handle to     \
    * translate. */                                                           \
   static inline void                                                         \
   vn_decode_##Enum##_temp(struct vn_cs_decoder *dec, Enum *val)              \
   {                                                                          \
      vn_decode_##Enum(dec, val);                                             \
   }                                                                          \
   static inline void vn_replace_##Enum##_handle(Enum *val)                   \
   {                                                                          \
      (void)val;                                                              \
   }

/*
 * StdVideoH264ScalingLists.
 *
 * Hand-serialized because its two members are fixed-size 2D arrays and the
 * generator's array handling understands only a single extent. Both extents
 * come from the STD_VIDEO_H264_* macros, so the encoded size is a compile-time
 * constant and cannot be influenced by the guest.
 *
 * The arrays are flattened deliberately: C guarantees 2D arrays are contiguous,
 * and the element type is uint8_t, so there is no padding or endianness concern.
 */
#define VN_H264_SCALING_4X4_TOTAL \
   (STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS * STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS)
#define VN_H264_SCALING_8X8_TOTAL \
   (STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS * STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS)

#define VN_H264_DEFINE_SCALING_LISTS_SERIALIZERS()                              \
   static inline size_t                                                                       \
   vn_sizeof_StdVideoH264ScalingLists(const StdVideoH264ScalingLists *val)                    \
   {                                                                                          \
      return vn_sizeof_uint16_t(&val->scaling_list_present_mask) +                            \
             vn_sizeof_uint16_t(&val->use_default_scaling_matrix_mask) +                      \
             vn_sizeof_blob_array(&val->ScalingList4x4[0][0], VN_H264_SCALING_4X4_TOTAL) + \
             vn_sizeof_blob_array(&val->ScalingList8x8[0][0], VN_H264_SCALING_8X8_TOTAL);  \
   }                                                                                          \
                                                                                              \
   static inline void                                                                         \
   vn_encode_StdVideoH264ScalingLists(struct vn_cs_encoder *enc,                              \
                                      const StdVideoH264ScalingLists *val)                    \
   {                                                                                          \
      vn_encode_uint16_t(enc, &val->scaling_list_present_mask);                               \
      vn_encode_uint16_t(enc, &val->use_default_scaling_matrix_mask);                         \
      vn_encode_blob_array(enc, &val->ScalingList4x4[0][0], VN_H264_SCALING_4X4_TOTAL);    \
      vn_encode_blob_array(enc, &val->ScalingList8x8[0][0], VN_H264_SCALING_8X8_TOTAL);    \
   }                                                                                          \
                                                                                              \
   static inline void                                                                         \
   vn_decode_StdVideoH264ScalingLists(struct vn_cs_decoder *dec,                              \
                                      StdVideoH264ScalingLists *val)                          \
   {                                                                                          \
      vn_decode_uint16_t(dec, &val->scaling_list_present_mask);                               \
      vn_decode_uint16_t(dec, &val->use_default_scaling_matrix_mask);                         \
      vn_decode_blob_array(dec, &val->ScalingList4x4[0][0], VN_H264_SCALING_4X4_TOTAL);    \
      vn_decode_blob_array(dec, &val->ScalingList8x8[0][0], VN_H264_SCALING_8X8_TOTAL);    \
   }                                                                                          \
                                                                                              \
   static inline void                                                                         \
   vn_decode_StdVideoH264ScalingLists_temp(struct vn_cs_decoder *dec,                         \
                                           StdVideoH264ScalingLists *val)                     \
   {                                                                                          \
      vn_decode_StdVideoH264ScalingLists(dec, val);                                           \
   }                                                                                          \
                                                                                              \
   static inline void                                                                         \
   vn_replace_StdVideoH264ScalingLists_handle(StdVideoH264ScalingLists *val)                  \
   {                                                                                          \
      (void)val;                                                                              \
   }                                                                                          \


#endif /* VN_PROTOCOL_VIDEO_H264_FLAGS_H */
