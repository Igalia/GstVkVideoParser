/* VideoParser
 * Copyright (C) 2022 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "gstvkh264dec.h"
#include "gstvkelements.h"
#include "glib_compat.h"


#include "videoutils.h"

#include "VulkanVideoParserIf.h"

#define GST_VK_H264_DEC(obj)           ((GstVkH264Dec *) obj)
#define GST_VK_H264_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE(obj), GstVkH264DecClass))
#define GST_VK_H264_DEC_CLASS(klass)   ((GstVkH264DecClass *) klass)

#define GST_CAT_DEFAULT gst_vk_parser_debug

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)NV12"));

typedef struct _VkH264Picture VkH264Picture;
struct _VkH264Picture
{
  StdVideoH264HrdParameters hrd;
  StdVideoH264SequenceParameterSetVui vui;
  StdVideoH264SequenceParameterSet sps;
  StdVideoH264PictureParameterSet pps;
  StdVideoH264ScalingLists scaling_lists_sps, scaling_lists_pps;
  int32_t offset_for_ref_frame[255];
};

typedef struct _GstVkH264Dec GstVkH264Dec;
struct _GstVkH264Dec
{
  GstH264Decoder parent;
  VkParserVideoDecodeClient *client;
  gboolean oob_pic_params;

  gint max_dpb_size;

  VkH264Picture vkp;
  GArray *refs;

  VkSharedBaseObj<VkParserVideoRefCountBase> spsclient, ppsclient;

  guint32 sps_update_count;
  guint32 pps_update_count;
};

struct VkPic
{
  VkPicIf *pic;
  VkParserPictureData data;
  GByteArray *bitstream;
  VkH264Picture vkp;
  uint8_t *slice_group_map;
  GArray *slice_offsets;
};

enum
{
  PROP_USER_DATA = 1,
  PROP_OOB_PIC_PARAMS,
};

G_DEFINE_TYPE(GstVkH264Dec, gst_vk_h264_dec, GST_TYPE_H264_DECODER)
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vkh264parse, "vkh264parse", GST_RANK_PRIMARY, GST_TYPE_VK_H264_DEC, vk_element_init(plugin));

static gpointer parent_class = NULL;

static StdVideoH264ProfileIdc
get_h264_profile (guint8 profile_idc)
{
  switch (static_cast<GstH264Profile>(profile_idc)) {
    case GST_H264_PROFILE_BASELINE:
      return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    case GST_H264_PROFILE_MAIN:
      return STD_VIDEO_H264_PROFILE_IDC_MAIN;
    case GST_H264_PROFILE_HIGH:
      return STD_VIDEO_H264_PROFILE_IDC_HIGH;
    case GST_H264_PROFILE_HIGH_444:
      return STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE;
    default:
      return STD_VIDEO_H264_PROFILE_IDC_INVALID;
  }
}

static StdVideoH264LevelIdc
get_h264_level_idc (guint8 level_idc)
{
  switch (level_idc) {
    case 10:
      return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case 11:
      return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case 12:
      return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case 13:
      return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case 20:
      return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case 21:
      return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case 22:
      return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case 30:
      return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case 31:
      return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case 32:
      return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case 40:
      return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case 41:
      return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case 42:
      return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case 50:
      return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case 51:
      return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case 52:
      return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case 60:
      return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case 61:
      return STD_VIDEO_H264_LEVEL_IDC_6_1;
    case 62:
      return STD_VIDEO_H264_LEVEL_IDC_6_2;
    default:
      return STD_VIDEO_H264_LEVEL_IDC_INVALID;
  }
}

static StdVideoH264ChromaFormatIdc
get_h264_chroma_format (guint8 chroma_format_idc)
{
  if (chroma_format_idc >= 0 && chroma_format_idc <= 3)
    return static_cast <StdVideoH264ChromaFormatIdc>(chroma_format_idc);
  return STD_VIDEO_H264_CHROMA_FORMAT_IDC_INVALID;
}

static StdVideoH264PocType
get_h264_poc_type (guint8 pic_order_cnt_type)
{
  if (pic_order_cnt_type >= 0 && pic_order_cnt_type <= 2)
    return static_cast <StdVideoH264PocType>(pic_order_cnt_type);
  return STD_VIDEO_H264_POC_TYPE_INVALID;
}

static StdVideoH264AspectRatioIdc
get_h264_aspect_ratio_idc (guint8 aspect_ratio_idc)
{
  if ((aspect_ratio_idc >= 0 && aspect_ratio_idc <= 16)
      || aspect_ratio_idc == 255)
    return static_cast <StdVideoH264AspectRatioIdc>(aspect_ratio_idc);
  return STD_VIDEO_H264_ASPECT_RATIO_IDC_INVALID;
}

static StdVideoH264WeightedBipredIdc
get_h264_weighted_bipred_idc (guint8 weighted_bipred_idc)
{
  if (weighted_bipred_idc >= 0 && weighted_bipred_idc <= 2)
    return static_cast <StdVideoH264WeightedBipredIdc>(weighted_bipred_idc);
  return STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_INVALID;
}

static VkPic *vk_pic_new (VkPicIf * pic)
{
  VkPic *vkpic = g_new0 (struct VkPic, 1);
  uint32_t zero = 0;

  vkpic->pic = pic;
  vkpic->bitstream = g_byte_array_new ();
  vkpic->slice_offsets = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  g_array_append_val (vkpic->slice_offsets, zero);
  return vkpic;
}

static void
vk_pic_free (gpointer data)
{
  VkPic *vkpic = static_cast<VkPic *>(data);
  if (vkpic->pic)
    vkpic->pic->Release ();
  g_byte_array_unref (vkpic->bitstream);
  g_free ((uint32_t *)vkpic->data.pSliceDataOffsets);
  g_free (vkpic->data.pBitstreamData);
  g_array_unref (vkpic->slice_offsets);
  g_free (vkpic->slice_group_map);
  g_free (vkpic);
}

static bool
profile_is_svc (GstCaps * caps)
{
  const GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *profile = gst_structure_get_string (structure, "profile");

  return g_str_has_prefix (profile, "scalable");
}

static GstFlowReturn
gst_vk_h264_dec_new_sequence (GstH264Decoder * decoder, const GstH264SPS * sps,
    gint max_dpb_size)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  GstVideoDecoder *dec = GST_VIDEO_DECODER (decoder);
  GstVideoCodecState *state;
  VkParserSequenceInfo seqInfo;
  guint dar_n = 0, dar_d = 0;

  seqInfo = VkParserSequenceInfo {
    .eCodec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT,
    .isSVC = profile_is_svc(decoder->input_state->caps),
    .frameRate = pack_framerate(GST_VIDEO_INFO_FPS_N(&decoder->input_state->info), GST_VIDEO_INFO_FPS_D(&decoder->input_state->info)) * 1000,
    .bProgSeq = sps->frame_mbs_only_flag,
    .nCodedWidth = sps->width,
    .nCodedHeight = sps->height,
    .nMaxWidth = 0,
    .nMaxHeight = 0,
    .nChromaFormat = sps->chroma_format_idc, // Chroma Format (0=4:0:0, 1=4:2:0, 2=4:2:2, 3=4:4:4)
    .uBitDepthLumaMinus8 = sps->bit_depth_luma_minus8, // Luma bit depth (0=8bit)
    .uBitDepthChromaMinus8 = sps->bit_depth_chroma_minus8, // Chroma bit depth (0=8bit)
    .cbSequenceHeader = 0, // Number of bytes in SequenceHeaderData
    .nMinNumDecodeSurfaces = max_dpb_size + 1,
    // .SequenceHeaderData[1024];
    .pbSideData = nullptr,
    .cbSideData = 0,
    .codecProfile = sps->profile_idc,
  };

  if (sps->frame_cropping_flag) {
    seqInfo.nDisplayWidth = sps->crop_rect_width;
    seqInfo.nDisplayHeight = sps->crop_rect_height;
  } else {
    seqInfo.nDisplayWidth = sps->width;
    seqInfo.nDisplayHeight = sps->height;
  }

  if (sps->vui_parameters_present_flag) {
    seqInfo.uVideoFullRange = sps->vui_parameters.video_full_range_flag;        // 0=16-235, 1=0-255
    if (sps->vui_parameters.nal_hrd_parameters_present_flag)
      seqInfo.lBitrate = sps->vui_parameters.nal_hrd_parameters.bit_rate_scale;
    else if (sps->vui_parameters.vcl_hrd_parameters_present_flag)
      seqInfo.lBitrate = sps->vui_parameters.vcl_hrd_parameters.bit_rate_scale;
    seqInfo.lVideoFormat = sps->vui_parameters.video_format;    // Video Format (VideoFormatXXX)
    seqInfo.lColorPrimaries = sps->vui_parameters.colour_primaries;     // Colour Primaries (ColorPrimariesXXX)
    seqInfo.lTransferCharacteristics =
        sps->vui_parameters.transfer_characteristics;
    seqInfo.lMatrixCoefficients = sps->vui_parameters.matrix_coefficients;
  }

  if (gst_video_calculate_display_ratio (&dar_n, &dar_d,
          seqInfo.nDisplayWidth, seqInfo.nDisplayHeight,
          GST_VIDEO_INFO_PAR_N (&decoder->input_state->info),
          GST_VIDEO_INFO_PAR_D (&decoder->input_state->info), 1, 1)) {
    seqInfo.lDARWidth = dar_n;
    seqInfo.lDARHeight = dar_d;
  }

  if (self->client)
    self->max_dpb_size = self->client->BeginSequence (&seqInfo);

  state =
      gst_video_decoder_set_output_state (dec, GST_VIDEO_FORMAT_NV12,
      seqInfo.nDisplayWidth, seqInfo.nDisplayHeight, decoder->input_state);
  gst_video_codec_state_unref (state);

  gst_video_decoder_negotiate (dec);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h264_dec_decode_slice (GstH264Decoder * decoder, GstH264Picture * picture,
    GstH264Slice * slice, GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  VkPic *vkpic = static_cast<VkPic *>(gst_h264_picture_get_user_data(picture));
  static const uint8_t nal[] = { 0, 0, 1 };
  uint32_t offset;

  vkpic->data.nNumSlices++;
  // nvidia parser adds 000001 NAL unit identifier at every slice
  g_byte_array_append (vkpic->bitstream, nal, sizeof (nal));
  g_byte_array_append (vkpic->bitstream, slice->nalu.data + slice->nalu.offset,
      slice->nalu.size);
  // GST_MEMDUMP_OBJECT(decoder, "SLICE :", slice->nalu.data + slice->nalu.offset, slice->nalu.size);
  offset =
      g_array_index (vkpic->slice_offsets, uint32_t,
      vkpic->slice_offsets->len - 1) + slice->nalu.size + sizeof (nal);
  g_array_append_val (vkpic->slice_offsets, offset);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h264_dec_new_picture (GstH264Decoder * decoder, GstVideoCodecFrame * frame,
    GstH264Picture * picture)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPicIf *pic = nullptr;
  VkPic *vkpic;

  if (self->client) {
    if (!self->client->AllocPictureBuffer (&pic))
      return GST_FLOW_ERROR;
  }

  vkpic = vk_pic_new (pic);
  gst_h264_picture_set_user_data (picture, vkpic, vk_pic_free);

  frame->output_buffer = gst_buffer_new ();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h264_dec_new_field_picture (GstH264Decoder * decoder,
    GstH264Picture * first_field, GstH264Picture * second_field)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPicIf *pic = nullptr;
  VkPic *vkpic;

  if (self->client) {
    if (!self->client->AllocPictureBuffer (&pic))
      return GST_FLOW_ERROR;
  }

  vkpic = vk_pic_new (pic);
  gst_h264_picture_set_user_data (second_field, vkpic, vk_pic_free);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h264_dec_output_picture (GstH264Decoder * decoder,
    GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h264_picture_get_user_data(picture));;

  if (self->client) {
    if (!self->client->DisplayPicture (vkpic->pic,
            picture->system_frame_number * frame->duration / 100)) {
      gst_h264_picture_unref (picture);
      return GST_FLOW_ERROR;
    }
  }

  gst_h264_picture_unref (picture);
  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (decoder), frame);
}

static GstFlowReturn
gst_vk_h264_dec_end_picture (GstH264Decoder * decoder, GstH264Picture * picture)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h264_picture_get_user_data(picture));
  gsize len;
  GstFlowReturn ret = GST_FLOW_OK;

  vkpic->data.pBitstreamData = g_byte_array_steal (vkpic->bitstream, &len);
  vkpic->data.nBitstreamDataLen = static_cast<int32_t>(len);
  vkpic->data.pSliceDataOffsets =
      static_cast <uint32_t *>(g_array_steal (vkpic->slice_offsets, NULL));

  if (self->client) {
    if (!self->client->DecodePicture (&vkpic->data))
      ret = GST_FLOW_ERROR;
  }

  return ret;
}

static uint8_t *
get_slice_group_map (GstH264PPS * pps)
{
  uint8_t *ret, i, j, k;

  ret = g_new0 (uint8_t, pps->pic_size_in_map_units_minus1 + 1);

  if (pps->num_slice_groups_minus1 == 0)
    return ret;

  switch (pps->slice_group_map_type) {
    case 0:{
      i = 0;
      do {
        for (j = 0;
            j <= pps->num_slice_groups_minus1
            && i <= pps->pic_size_in_map_units_minus1;
            i += pps->run_length_minus1[j++] + 1) {
          for (k = 0; k <= pps->run_length_minus1[j]
              && i + k <= pps->pic_size_in_map_units_minus1; k++)
            ret[i + k] = j;
        }
      } while (i <= pps->pic_size_in_map_units_minus1);
      break;
    }
    case 1:{
      for (i = 0; i <= pps->pic_size_in_map_units_minus1; i++) {
        ret[i] = ((i % (pps->sequence->pic_width_in_mbs_minus1 + 1)) +
            (((i / (pps->sequence->pic_width_in_mbs_minus1 + 1)) *
                    (pps->num_slice_groups_minus1 + 1)) / 2)) %
            (pps->num_slice_groups_minus1 + 1);
      }
      break;
    }
    case 2:{
      uint32_t ytl, xtl, ybr, xbr;

      for (i = 0; i <= pps->pic_size_in_map_units_minus1; i++)
        ret[i] = pps->num_slice_groups_minus1;
      for (i = pps->num_slice_groups_minus1 - 1; i >= 0; i--) {
        ytl = pps->top_left[i] / (pps->sequence->pic_width_in_mbs_minus1 + 1);
        xtl = pps->top_left[i] % (pps->sequence->pic_width_in_mbs_minus1 + 1);
        ybr =
            pps->bottom_right[i] / (pps->sequence->pic_width_in_mbs_minus1 + 1);
        xbr =
            pps->bottom_right[i] % (pps->sequence->pic_width_in_mbs_minus1 + 1);

        for (j = ytl; j < ybr; j++)
          for (k = xtl; k < xbr; k++)
            ret[j * (pps->sequence->pic_width_in_mbs_minus1 + 1) + k] = i;
      }
      break;
    }
    case 3:
    case 4:
    case 5:
      /* @TODO */
      break;
    case 6:
      for (i = 0; i <= pps->pic_size_in_map_units_minus1; i++)
        ret[i] = pps->slice_group_id[i];
      break;
    default:
      break;
  };

  return ret;
}

static void
fill_sps (GstH264SPS * sps, VkH264Picture * vkp)
{
  GstH264VUIParams *vui = &sps->vui_parameters;
  GstH264HRDParams *hrd = NULL;

  if (sps->scaling_matrix_present_flag) {
    vkp->scaling_lists_sps.scaling_list_present_mask = 1;
    vkp->scaling_lists_sps.use_default_scaling_matrix_mask = 0;

    memcpy (&vkp->scaling_lists_sps.ScalingList4x4, &sps->scaling_lists_4x4,
        sizeof (vkp->scaling_lists_sps.ScalingList4x4));
    memcpy (&vkp->scaling_lists_sps.ScalingList8x8, &sps->scaling_lists_8x8,
        sizeof (vkp->scaling_lists_sps.ScalingList8x8));
  }

  if (sps->num_ref_frames_in_pic_order_cnt_cycle > 0) {
    for (uint32_t i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
      vkp->offset_for_ref_frame[i] = sps->offset_for_ref_frame[i];
  }

  if (vui->nal_hrd_parameters_present_flag)
    hrd = &vui->nal_hrd_parameters;
  else if (vui->vcl_hrd_parameters_present_flag)
    hrd = &vui->vcl_hrd_parameters;

  if (hrd) {
    vkp->hrd = StdVideoH264HrdParameters {
      .cpb_cnt_minus1 = hrd->cpb_cnt_minus1,
      .bit_rate_scale = hrd->bit_rate_scale,
      .cpb_size_scale = hrd->cpb_size_scale,
      .initial_cpb_removal_delay_length_minus1 =
          hrd->initial_cpb_removal_delay_length_minus1,
      .cpb_removal_delay_length_minus1 = hrd->cpb_removal_delay_length_minus1,
      .dpb_output_delay_length_minus1 = hrd->dpb_output_delay_length_minus1,
      .time_offset_length = hrd->time_offset_length,
    };

    memcpy (&vkp->hrd.bit_rate_value_minus1, hrd->bit_rate_value_minus1,
        sizeof (vkp->hrd.bit_rate_value_minus1));
    memcpy (&vkp->hrd.cpb_size_value_minus1, hrd->cpb_size_value_minus1,
        sizeof (vkp->hrd.cpb_size_value_minus1));
  }

  vkp->vui = StdVideoH264SequenceParameterSetVui {
    .flags = {
      .aspect_ratio_info_present_flag = vui->aspect_ratio_info_present_flag,
      .overscan_info_present_flag = vui->overscan_info_present_flag,
      .overscan_appropriate_flag = vui->overscan_appropriate_flag,
      .video_signal_type_present_flag = vui->video_signal_type_present_flag,
      .video_full_range_flag = vui->video_full_range_flag,
      .color_description_present_flag = vui->colour_description_present_flag,
      .chroma_loc_info_present_flag = vui->chroma_loc_info_present_flag,
      .timing_info_present_flag = vui->timing_info_present_flag,
      .fixed_frame_rate_flag = vui->fixed_frame_rate_flag,
      .bitstream_restriction_flag = vui->bitstream_restriction_flag,
      .nal_hrd_parameters_present_flag = vui->nal_hrd_parameters_present_flag,
      .vcl_hrd_parameters_present_flag = vui->vcl_hrd_parameters_present_flag,
    },
    .aspect_ratio_idc = get_h264_aspect_ratio_idc(vui->aspect_ratio_idc),
    .sar_width = vui->sar_width,
    .sar_height = vui->sar_height,
    .video_format = vui->video_format,
    .colour_primaries = vui->colour_primaries,
    .transfer_characteristics = vui->transfer_characteristics,
    .matrix_coefficients = vui->matrix_coefficients,
    .num_units_in_tick = vui->num_units_in_tick,
    .time_scale = vui->time_scale,
    .max_num_reorder_frames = static_cast<uint8_t>(vui->num_reorder_frames),
    .max_dec_frame_buffering =
        static_cast<uint8_t>(vui->max_dec_frame_buffering),
    .chroma_sample_loc_type_top_field = vui->chroma_sample_loc_type_top_field,
    .chroma_sample_loc_type_bottom_field = vui->chroma_sample_loc_type_bottom_field,
    .pHrdParameters = hrd ? &vkp->hrd : NULL,
  };

  vkp->sps = StdVideoH264SequenceParameterSet {
    .flags = {
      .constraint_set0_flag = sps->constraint_set0_flag,
      .constraint_set1_flag = sps->constraint_set1_flag,
      .constraint_set2_flag = sps->constraint_set2_flag,
      .constraint_set3_flag = sps->constraint_set3_flag,
      .constraint_set4_flag = sps->constraint_set4_flag,
      .constraint_set5_flag = sps->constraint_set5_flag,
      .direct_8x8_inference_flag = sps->direct_8x8_inference_flag,
      .mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag,
      .frame_mbs_only_flag = sps->frame_mbs_only_flag,
      .delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag,
      .separate_colour_plane_flag = sps->separate_colour_plane_flag,
      .gaps_in_frame_num_value_allowed_flag =
          sps->gaps_in_frame_num_value_allowed_flag,
      .qpprime_y_zero_transform_bypass_flag =
          sps->qpprime_y_zero_transform_bypass_flag,
      .frame_cropping_flag = sps->frame_cropping_flag,
      .seq_scaling_matrix_present_flag = sps->scaling_matrix_present_flag,
      .vui_parameters_present_flag = sps->vui_parameters_present_flag,
    },
    .profile_idc = get_h264_profile(sps->profile_idc),
    .level_idc = get_h264_level_idc(sps->level_idc),
    .chroma_format_idc = get_h264_chroma_format(sps->chroma_format_idc),
    .seq_parameter_set_id = static_cast<uint8_t>(sps->id),
    .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
    .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
    .log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4,
    .pic_order_cnt_type = get_h264_poc_type(sps->pic_order_cnt_type),
    .offset_for_non_ref_pic = sps->offset_for_non_ref_pic,
    .offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field,
    .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
    .num_ref_frames_in_pic_order_cnt_cycle =
        sps->num_ref_frames_in_pic_order_cnt_cycle,
    .max_num_ref_frames = static_cast<uint8_t>(sps->num_ref_frames),
    .pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1,
    .pic_height_in_map_units_minus1 = sps->pic_height_in_map_units_minus1,
    .frame_crop_left_offset = sps->frame_crop_left_offset,
    .frame_crop_right_offset = sps->frame_crop_right_offset,
    .frame_crop_top_offset = sps->frame_crop_top_offset,
    .frame_crop_bottom_offset = sps->frame_crop_bottom_offset,
    .pOffsetForRefFrame = (sps->num_ref_frames_in_pic_order_cnt_cycle > 0) ?
        vkp->offset_for_ref_frame : nullptr,
    .pScalingLists = sps->scaling_matrix_present_flag ?
        &vkp->scaling_lists_sps : nullptr,
    .pSequenceParameterSetVui = sps->vui_parameters_present_flag ?
        &vkp->vui : nullptr,
  };
}

static void
fill_pps (GstH264PPS * pps, VkH264Picture * vkp)
{
  if (pps->pic_scaling_matrix_present_flag) {
    vkp->scaling_lists_pps.scaling_list_present_mask = 1;
    vkp->scaling_lists_pps.use_default_scaling_matrix_mask = 0;

    memcpy (&vkp->scaling_lists_pps.ScalingList4x4, &pps->scaling_lists_4x4,
        sizeof (vkp->scaling_lists_pps.ScalingList4x4));
    memcpy (&vkp->scaling_lists_pps.ScalingList8x8, &pps->scaling_lists_8x8,
        sizeof (vkp->scaling_lists_pps.ScalingList8x8));
  }

  vkp->pps = StdVideoH264PictureParameterSet {
    .flags = {
      .transform_8x8_mode_flag = pps->transform_8x8_mode_flag,
      .redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag,
      .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
      .deblocking_filter_control_present_flag =
          pps->deblocking_filter_control_present_flag,
      .weighted_pred_flag = pps->weighted_pred_flag,
      .bottom_field_pic_order_in_frame_present_flag = 0, //FIXME
      .entropy_coding_mode_flag = pps->entropy_coding_mode_flag,
      .pic_scaling_matrix_present_flag = pps->pic_scaling_matrix_present_flag,
    },
    .seq_parameter_set_id = static_cast<uint8_t>(pps->sequence->id),
    .pic_parameter_set_id = static_cast<uint8_t>(pps->id),
    .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_active_minus1,
    .weighted_bipred_idc = get_h264_weighted_bipred_idc(pps->weighted_bipred_idc),
    .pic_init_qp_minus26 = pps->pic_init_qp_minus26,
    .pic_init_qs_minus26 = pps->pic_init_qs_minus26,
    .chroma_qp_index_offset = pps->chroma_qp_index_offset,
    .second_chroma_qp_index_offset =
        static_cast<int8_t>(pps->second_chroma_qp_index_offset),
    .pScalingLists = pps->pic_scaling_matrix_present_flag ?
        &vkp->scaling_lists_pps : NULL,
  };
}

static void
fill_dbp_entry (VkParserH264DpbEntry * entry, GstH264Picture * picture)
{
  auto vkpic =
      reinterpret_cast <VkPic *>(gst_h264_picture_get_user_data (picture));
  if (!vkpic) {
    *entry = { 0, };
    return;
  }

  *entry = VkParserH264DpbEntry {
    .pPicBuf = vkpic->pic,
    .FrameIdx = GST_H264_PICTURE_IS_LONG_TERM_REF (picture) ? picture->long_term_pic_num : picture->pic_num,
    .is_long_term = GST_H264_PICTURE_IS_LONG_TERM_REF (picture),
    .not_existing = picture->nonexisting,
  };

  // used_for_reference: 0=unused, 1=top_field, 2=bottom_field, 3=both_fields
  switch (picture->field) {
    case GST_H264_PICTURE_FIELD_FRAME:
      entry->used_for_reference = 3;
      entry->FieldOrderCnt[0] = picture->top_field_order_cnt;
      entry->FieldOrderCnt[1] = picture->bottom_field_order_cnt;
      break;
    case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
      if (picture->other_field) {
        entry->FieldOrderCnt[0] = picture->other_field->top_field_order_cnt;
        entry->used_for_reference = 3;
      } else {
        entry->FieldOrderCnt[0] = 0;
        entry->used_for_reference = 2;
      }
      entry->FieldOrderCnt[1] = picture->bottom_field_order_cnt;
      break;
    case GST_H264_PICTURE_FIELD_TOP_FIELD:
      entry->FieldOrderCnt[0] = picture->top_field_order_cnt;
      if (picture->other_field) {
        entry->FieldOrderCnt[1] = picture->other_field->bottom_field_order_cnt;
        entry->used_for_reference = 3;
      } else {
        entry->FieldOrderCnt[1] = 0;
        entry->used_for_reference = 2;
      }
      break;
    default:
      entry->used_for_reference = 0;
      break;
  }
}

static GstFlowReturn
gst_vk_h264_dec_start_picture (GstH264Decoder * decoder, GstH264Picture * picture,
    GstH264Slice * slice, GstH264Dpb * dpb)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPic *vkpic =
      reinterpret_cast <VkPic *>(gst_h264_picture_get_user_data (picture));
  VkH264Picture *vkp = &self->vkp;
  GstH264PPS *pps = slice->header.pps;
  GstH264SPS *sps = pps->sequence;

  if (!self->oob_pic_params
      || (self->sps_update_count == 0 && self->sps_update_count == 0)) {
    vkp = &vkpic->vkp;
    fill_sps (sps, vkp);
    fill_pps (pps, vkp);
  }

  vkpic->data = VkParserPictureData {
    .PicWidthInMbs = sps->width / 16, // Coded Frame Size
    .FrameHeightInMbs = sps->height / 16, // Coded Frame Height
    .pCurrPic = vkpic->pic,
    .field_pic_flag = slice->header.field_pic_flag, // 0=frame picture, 1=field picture
    .bottom_field_flag = slice->header.bottom_field_flag, // 0=top field, 1=bottom field (ignored if field_pic_flag=0)
    .second_field = picture->second_field, // Second field of a complementary field pair
    .progressive_frame = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) == 0, // Frame is progressive
    .top_field_first = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0,
    .repeat_first_field = 0, // For 3:2 pulldown (number of additional fields,
                             // 2=frame doubling, 4=frame tripling)
    .ref_pic_flag = picture->ref_pic, // Frame is a reference frame
    .intra_pic_flag =
        GST_H264_IS_I_SLICE (&slice->header)
        || GST_H264_IS_SI_SLICE (&slice->header), // Frame is entirely intra coded (no temporal dependencies)
    .chroma_format = sps->chroma_format_idc, // Chroma Format (should match sequence info)
    .picture_order_count = picture->pic_order_cnt, // picture order count (if known)

    .pbSideData = nullptr, // Encryption Info
    .nSideDataLen = 0, // Encryption Info length

    // Bitstream data
    //.nBitstreamDataLen, // Number of bytes in bitstream data buffer
    //.pBitstreamData, // Ptr to bitstream data for this picture (slice-layer)
    //.pSliceDataOffsets, // nNumSlices entries, contains offset of each slice
    // within the bitstream data buffer
    .current_dpb_id = 0, //FIXME: This value was not used in NV parser 0.9.7
  };

  VkParserH264PictureData *h264 = &vkpic->data.CodecSpecific.h264;
  *h264 = VkParserH264PictureData {
    .pStdSps = &vkp->sps,
    .pSpsClientObject = self->spsclient,
    .pStdPps = &vkp->pps,
    .pPpsClientObject = self->ppsclient,
    .pic_parameter_set_id = static_cast<uint8_t>(pps->id),          // PPS ID
    .seq_parameter_set_id = static_cast<uint8_t>(pps->sequence->id),          // SPS ID
    .num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1,
    .weighted_pred_flag = pps->weighted_pred_flag,
    .weighted_bipred_idc = pps->weighted_bipred_idc,
    .pic_init_qp_minus26 = pps->pic_init_qp_minus26,
    .redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag,
    .deblocking_filter_control_present_flag =
        pps->deblocking_filter_control_present_flag,
    .transform_8x8_mode_flag = pps->transform_8x8_mode_flag,
    .MbaffFrameFlag =
        (sps->mb_adaptive_frame_field_flag && !slice->header.field_pic_flag),
    .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
    .entropy_coding_mode_flag = pps->entropy_coding_mode_flag,
    .pic_order_present_flag = pps->pic_order_present_flag,
    .chroma_qp_index_offset = pps->chroma_qp_index_offset,
    .second_chroma_qp_index_offset =
        static_cast<int8_t>(pps->second_chroma_qp_index_offset),
    .frame_num = picture->frame_num,
    .CurrFieldOrderCnt =
        { picture->top_field_order_cnt, picture->bottom_field_order_cnt },
    .fmo_aso_enable = pps->num_slice_groups_minus1 > 0,
    .num_slice_groups_minus1 =
        static_cast<uint8_t>(pps->num_slice_groups_minus1),
    .slice_group_map_type = pps->slice_group_map_type,
    .pic_init_qs_minus26 = pps->pic_init_qp_minus26,
    .slice_group_change_rate_minus1 = pps->slice_group_change_rate_minus1,
    .pMb2SliceGroupMap =
        (vkpic->slice_group_map = get_slice_group_map (pps),
         vkpic->slice_group_map),
    // // DPB
    // VkParserH264DpbEntry dpb[16 + 1]; // List of reference frames within the DPB
  };

  /* reference frames */
  {
    guint i, ref_frame_idx = 0;

    gst_h264_dpb_get_pictures_short_term_ref (dpb, FALSE, FALSE, self->refs);
    for (i = 0; ref_frame_idx < 16 + 1 && i < self->refs->len; i++) {
      GstH264Picture *pic = g_array_index (self->refs, GstH264Picture *, i);
      fill_dbp_entry (&h264->dpb[ref_frame_idx++], pic);
    }
    g_array_set_size (self->refs, 0);

    gst_h264_dpb_get_pictures_long_term_ref (dpb, FALSE, self->refs);
    for (; ref_frame_idx < 16 + 1 && i < self->refs->len; i++) {
      GstH264Picture *pic = g_array_index (self->refs, GstH264Picture *, i);
      fill_dbp_entry (&h264->dpb[ref_frame_idx++], pic);
    }
    g_array_set_size (self->refs, 0);

    for (; ref_frame_idx < 16 + 1; ref_frame_idx++)
      h264->dpb[ref_frame_idx] = { 0, };
  }

  return GST_FLOW_OK;
}

static void
gst_vk_h264_dec_unhandled_nalu (GstH264Decoder * decoder, const guint8 * data,
    guint32 size)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);

  if (self->client)
    self->client->UnhandledNALU (data, size);
}

static void
gst_vk_h264_dec_update_picture_parameters (GstH264Decoder * decoder,
    GstH264NalUnitType type, const gpointer nalu)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (decoder);
  VkPictureParameters params;

  switch (type) {
    case GST_H264_NAL_SPS:{
      GstH264SPS *sps = static_cast < GstH264SPS * >(nalu);
      fill_sps (sps, &self->vkp);
      params = VkPictureParameters {
        .updateType = VK_PICTURE_PARAMETERS_UPDATE_H264_SPS,
        .pH264Sps = &self->vkp.sps,
        .updateSequenceCount = self->sps_update_count++,
      };
      if (self->client) {
        if (!self->client->UpdatePictureParameters (&params, self->spsclient,
                params.updateSequenceCount))
          GST_ERROR_OBJECT (self, "Failed to update sequence parameters");
      }
      break;
    }
    case GST_H264_NAL_PPS:{
      GstH264PPS *pps = static_cast < GstH264PPS * >(nalu);
      fill_pps (pps, &self->vkp);
      params = VkPictureParameters {
        .updateType = VK_PICTURE_PARAMETERS_UPDATE_H264_PPS,
        .pH264Pps = &self->vkp.pps,
        .updateSequenceCount = self->pps_update_count++,
      };
      if (self->client) {
        if (!self->client->UpdatePictureParameters (&params, self->ppsclient,
                params.updateSequenceCount))
          GST_ERROR_OBJECT (self, "Failed to update picture parameters");
      }
      break;
    }
    default:
      break;
  }
}

static void
gst_vk_h264_dec_dispose (GObject * object)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (object);

  if (self->spsclient)
    self->spsclient->Release ();
  if (self->ppsclient)
    self->ppsclient->Release ();

  g_clear_pointer (&self->refs, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_vk_h264_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVkH264Dec *self = GST_VK_H264_DEC (object);

  switch (property_id) {
    case PROP_USER_DATA:
      self->client =
          reinterpret_cast <VkParserVideoDecodeClient *>(g_value_get_pointer (value));
      break;
    case PROP_OOB_PIC_PARAMS:
      self->oob_pic_params = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vk_h264_dec_class_init (GstVkH264DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_set_static_metadata (element_class, "Vulkan H264 parser",
      "Filter/Analyzer/Video",
      "Generates Vulkan Video structures for H264 bitstream",
      "Víctor Jáquez <vjaquez@igalia.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gobject_class->dispose = gst_vk_h264_dec_dispose;
  gobject_class->set_property = gst_vk_h264_dec_set_property;

  h264decoder_class->new_sequence = gst_vk_h264_dec_new_sequence;
  h264decoder_class->decode_slice = gst_vk_h264_dec_decode_slice;
  h264decoder_class->new_picture = gst_vk_h264_dec_new_picture;
  h264decoder_class->output_picture = gst_vk_h264_dec_output_picture;
  h264decoder_class->start_picture = gst_vk_h264_dec_start_picture;
  h264decoder_class->end_picture = gst_vk_h264_dec_end_picture;
  h264decoder_class->new_field_picture = gst_vk_h264_dec_new_field_picture;
  h264decoder_class->unhandled_nalu = gst_vk_h264_dec_unhandled_nalu;
  h264decoder_class->update_picture_parameters =
      gst_vk_h264_dec_update_picture_parameters;

  g_object_class_install_property (gobject_class, PROP_USER_DATA,
      g_param_spec_pointer ("user-data", "user-data", "user-data",
          GParamFlags (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));

  g_object_class_install_property (gobject_class, PROP_OOB_PIC_PARAMS,
      g_param_spec_boolean ("oob-pic-params", "oob-pic-params",
          "oop-pic-params", FALSE,
          GParamFlags (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));
}

static void
gst_vk_h264_dec_init (GstVkH264Dec * self)
{
  gst_h264_decoder_set_process_ref_pic_lists (GST_H264_DECODER (self), FALSE);

  self->refs = g_array_sized_new (FALSE, TRUE, sizeof (GstH264Decoder *), 16);
  g_array_set_clear_func (self->refs, (GDestroyNotify) gst_clear_h264_picture);
}
