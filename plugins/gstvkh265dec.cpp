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

#include "gstvkelements.h"
#include "glib_compat.h"

#include <atomic>

#include "videoutils.h"
#include "VulkanVideoParserIf.h"
#include "vulkan_video_codec_h265std.h"

#define GST_VK_H265_DEC(obj)           ((GstVkH265Dec *) obj)
#define GST_VK_H265_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE(obj), GstVkH265DecClass))
#define GST_VK_H265_DEC_CLASS(klass)   ((GstVkH265DecClass *) klass)


#define GST_CAT_DEFAULT gst_vk_parser_debug

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)NV12"));

typedef struct _VkH265Picture VkH265Picture;
struct _VkH265Picture
{
  StdVideoH265HrdParameters hrd;
  StdVideoH265SequenceParameterSetVui vui;
  StdVideoH265ProfileTierLevel profileTierLevel;
  StdVideoH265SequenceParameterSet sps;
  StdVideoH265PictureParameterSet pps;
  StdVideoH265VideoParameterSet vps;
  StdVideoH265DecPicBufMgr pic_buf_mgr;
  StdVideoH265ScalingLists scaling_lists_sps, scaling_lists_pps;
};

typedef struct _GstVkH265Dec GstVkH265Dec;
struct _GstVkH265Dec
{
  GstH265Decoder parent;
  VkParserVideoDecodeClient *client;
  gboolean oob_pic_params;

  gint max_dpb_size;

  GstH265SPS last_sps;
  GstH265PPS last_pps;
  GstH265VPS last_vps;
  VkH265Picture vkp;
  GArray *refs;

  VkSharedBaseObj<VkParserVideoRefCountBase> spsclient, ppsclient, vpsclient;

  guint32 sps_update_count;
  guint32 pps_update_count;
};

struct VkPic
{
  VkPicIf *pic;
  VkParserPictureData data;
  GByteArray *bitstream;
  VkH265Picture vkp;
  uint8_t *slice_group_map;
  GArray *slice_offsets;
};

enum
{
  PROP_USER_DATA = 1,
  PROP_OOB_PIC_PARAMS,
};

G_DEFINE_TYPE(GstVkH265Dec, gst_vk_h265_dec, GST_TYPE_H265_DECODER)
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vkh265parse, "vkh265parse", GST_RANK_PRIMARY, GST_TYPE_VK_H265_DEC, vk_element_init(plugin));

static gpointer parent_class = NULL;

static VkPic
*vk_pic_new (VkPicIf * pic)
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

static StdVideoH265ProfileIdc
get_profile_idc (GstH265ProfileIDC profile_idc)
{
  switch (profile_idc) {
  case GST_H265_PROFILE_IDC_MAIN:
    return STD_VIDEO_H265_PROFILE_IDC_MAIN;
  case GST_H265_PROFILE_IDC_MAIN_10:
    return STD_VIDEO_H265_PROFILE_IDC_MAIN_10;
  case GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE:
    return STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE;
  case GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION:
    return STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS;
  default:
    break;
  }

  return STD_VIDEO_H265_PROFILE_IDC_INVALID;
}

static VkPic *
gst_vk_h265_dec_get_decoder_frame_from_picture (GstH265Decoder * self,
    GstH265Picture * picture)
{
  VkPic *frame;

  frame = (VkPic *) gst_h265_picture_get_user_data (picture);

  if (!frame)
    GST_DEBUG_OBJECT (self, "current picture does not have decoder frame");

  return frame;
}

static GstFlowReturn
gst_vk_h265_dec_new_sequence (GstH265Decoder * decoder, const GstH265SPS * sps,
    gint max_dpb_size)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  GstVideoDecoder *dec = GST_VIDEO_DECODER (decoder);
  GstVideoCodecState *state;
  VkParserSequenceInfo seqInfo;
  guint dar_n = 0, dar_d = 0;

  seqInfo = VkParserSequenceInfo {
    .eCodec = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT,
    .isSVC = profile_is_svc(decoder->input_state->caps),
    .frameRate = pack_framerate(GST_VIDEO_INFO_FPS_N(&decoder->input_state->info), GST_VIDEO_INFO_FPS_D(&decoder->input_state->info)),
    .bProgSeq = true, // Progressive by default
    .nCodedWidth = sps->width,
    .nCodedHeight = sps->height,
    .nMaxWidth = 0,
    .nMaxHeight = 0,
    .nChromaFormat = sps->chroma_format_idc, // Chroma Format (0=4:0:0, 1=4:2:0, 2=4:2:2, 3=4:4:4)
    .uBitDepthLumaMinus8 = sps->bit_depth_luma_minus8, // Luma bit depth (0=8bit)
    .uBitDepthChromaMinus8 = sps->bit_depth_chroma_minus8, // Chroma bit depth (0=8bit)
    .cbSequenceHeader = 0, // Number of bytes in SequenceHeaderData
    .nMinNumDecodeSurfaces = MIN (max_dpb_size + 1, 8), // FIXME: Seems that max is not the right value 8 is a max for NVidia
    // .SequenceHeaderData[1024];
    .pbSideData = nullptr,
    .cbSideData = 0,
    .codecProfile = static_cast<uint32_t>(get_profile_idc(static_cast<GstH265ProfileIDC>(sps->profile_tier_level.profile_idc))),
  };

  if (sps->vui_parameters_present_flag && sps->vui_params.field_seq_flag) {
    seqInfo.bProgSeq = false;
  } else {
    /* 7.4.4 Profile, tier and level sementics */
    if (sps->profile_tier_level.progressive_source_flag &&
        !sps->profile_tier_level.interlaced_source_flag) {
      seqInfo.bProgSeq = true;
    } else {
      seqInfo.bProgSeq = true;
    }
  }

  if (sps->conformance_window_flag) {
    seqInfo.nDisplayWidth = sps->crop_rect_width;
    seqInfo.nDisplayHeight = sps->crop_rect_height;
  } else {
    seqInfo.nDisplayWidth = sps->width;
    seqInfo.nDisplayHeight = sps->height;
  }

  if (sps->vui_parameters_present_flag) {
    seqInfo.uVideoFullRange = sps->vui_params.video_full_range_flag; // 0=16-235, 1=0-255
    seqInfo.lVideoFormat = sps->vui_params.video_format;    // Video Format (VideoFormatXXX)
    seqInfo.lColorPrimaries = sps->vui_params.colour_primaries;     // Colour Primaries (ColorPrimariesXXX)
    seqInfo.lTransferCharacteristics = sps->vui_params.transfer_characteristics;
    seqInfo.lMatrixCoefficients = sps->vui_params.matrix_coefficients;
    seqInfo.lBitrate = sps->vui_params.hrd_params.bit_rate_scale;
  } else if  (sps->vps) {
    seqInfo.lBitrate = sps->vps->hrd_params.bit_rate_scale;
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
gst_vk_h265_dec_decode_slice (GstH265Decoder * decoder, GstH265Picture * picture,
    GstH265Slice * slice, GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  VkPic *vkpic = static_cast<VkPic *>(gst_h265_picture_get_user_data(picture));
  static const uint8_t nal[] = { 0, 0, 1 };
  const size_t start_code_size = sizeof(nal);
  uint32_t offset;

  vkpic->data.nNumSlices++;
  // nvidia parser adds 000001 NAL unit identifier at every slice
  g_byte_array_append (vkpic->bitstream, nal, start_code_size);
  g_byte_array_append (vkpic->bitstream, slice->nalu.data + slice->nalu.offset,
      slice->nalu.size);
  // GST_MEMDUMP_OBJECT(decoder, "SLICE :", slice->nalu.data + slice->nalu.offset, slice->nalu.size);
  offset =
      g_array_index (vkpic->slice_offsets, uint32_t,
      vkpic->slice_offsets->len - 1) + slice->nalu.size + start_code_size;
  g_array_append_val (vkpic->slice_offsets, offset);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h265_dec_new_picture (GstH265Decoder * decoder, GstVideoCodecFrame * frame,
    GstH265Picture * picture)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  VkPicIf *pic = nullptr;
  VkPic *vkpic;

  if (self->client) {
    if (!self->client->AllocPictureBuffer (&pic))
      return GST_FLOW_ERROR;
  }

  vkpic = vk_pic_new (pic);
  gst_h265_picture_set_user_data (picture, vkpic, vk_pic_free);

  frame->output_buffer = gst_buffer_new ();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vk_h265_dec_output_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h265_picture_get_user_data(picture));;

  if (self->client) {
    if (!self->client->DisplayPicture (vkpic->pic,
            //FIXME: Why divided by 100  ???
            picture->system_frame_number * frame->duration / 100)) {
      gst_h265_picture_unref (picture);
      return GST_FLOW_ERROR;
    }
  }

  gst_h265_picture_unref (picture);
  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (decoder), frame);
}

static GstFlowReturn
gst_vk_h265_dec_end_picture (GstH265Decoder * decoder, GstH265Picture * picture)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h265_picture_get_user_data(picture));
  gsize len;
  uint32_t *slice_offsets;
  GstFlowReturn ret = GST_FLOW_OK;

  vkpic->data.pBitstreamData = g_byte_array_steal (vkpic->bitstream, &len);
  vkpic->data.nBitstreamDataLen = static_cast<int32_t>(len);
  vkpic->data.pSliceDataOffsets = slice_offsets =
      static_cast <uint32_t *>(g_array_steal (vkpic->slice_offsets, NULL));

  // FIXME: This flag is set to TRUE unconditionally because VulkanVideoParser.cpp expects it be true
  // during the decode phase. It will be set to True by base class only when it will be added to the dpb
  // See VulkanVideoParser.cpp+1862 in VulkanVideoParser::DecodePicture
  vkpic->data.ref_pic_flag = TRUE;

  if (self->client) {
    if (!self->client->DecodePicture (&vkpic->data))
      ret = GST_FLOW_ERROR;
  }

  g_free (vkpic->data.pBitstreamData);
  g_free (slice_offsets);

  return ret;
}

static void
fill_scaling_list(GstH265ScalingList* src, StdVideoH265ScalingLists* dest) {
  guint i;
  memcpy (&dest->ScalingList4x4, &src->scaling_lists_4x4,
      sizeof (dest->ScalingList4x4));
  memcpy (&dest->ScalingList8x8, &src->scaling_lists_8x8,
      sizeof (dest->ScalingList8x8));
  memcpy (&dest->ScalingList16x16, &src->scaling_lists_16x16,
      sizeof (dest->ScalingList16x16));
  memcpy (&dest->ScalingList32x32, &src->scaling_lists_32x32,
      sizeof (dest->ScalingList32x32));

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++) {
    dest->ScalingListDCCoef16x16[i] =
    src->scaling_list_dc_coef_minus8_16x16[i] + 8;
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++) {
    dest->ScalingListDCCoef32x32[i] =
        src->scaling_list_dc_coef_minus8_32x32[i] + 8;
  }
}

static void
fill_sps(GstH265SPS* sps, VkH265Picture* vkp)
{
    if (sps->vui_parameters_present_flag) {
      if (sps->vui_params.hrd_parameters_present_flag) {
        vkp->hrd = StdVideoH265HrdParameters {
          .flags = StdVideoH265HrdFlags {
                .nal_hrd_parameters_present_flag = sps->vui_params.hrd_params.nal_hrd_parameters_present_flag,
                .vcl_hrd_parameters_present_flag = sps->vui_params.hrd_params.vcl_hrd_parameters_present_flag,
                .sub_pic_hrd_params_present_flag = sps->vui_params.hrd_params.sub_pic_hrd_params_present_flag,
                .sub_pic_cpb_params_in_pic_timing_sei_flag = sps->vui_params.hrd_params.sub_pic_cpb_params_in_pic_timing_sei_flag,
                .fixed_pic_rate_general_flag = sps->vui_params.hrd_params.fixed_pic_rate_general_flag[0],
                .fixed_pic_rate_within_cvs_flag = sps->vui_params.hrd_params.fixed_pic_rate_within_cvs_flag[0],
                .low_delay_hrd_flag = sps->vui_params.hrd_params.low_delay_hrd_flag[0],
           },
            .tick_divisor_minus2 = sps->vui_params.hrd_params.tick_divisor_minus2,
            .du_cpb_removal_delay_increment_length_minus1 = sps->vui_params.hrd_params.du_cpb_removal_delay_increment_length_minus1,
            .dpb_output_delay_du_length_minus1 = sps->vui_params.hrd_params.dpb_output_delay_du_length_minus1,
            .bit_rate_scale = sps->vui_params.hrd_params.bit_rate_scale,
            .cpb_size_scale = sps->vui_params.hrd_params.cpb_size_scale,
            .cpb_size_du_scale = sps->vui_params.hrd_params.cpb_size_du_scale,
            .initial_cpb_removal_delay_length_minus1 = sps->vui_params.hrd_params.initial_cpb_removal_delay_length_minus1,
            .au_cpb_removal_delay_length_minus1 = sps->vui_params.hrd_params.au_cpb_removal_delay_length_minus1,
            .dpb_output_delay_length_minus1 = sps->vui_params.hrd_params.dpb_output_delay_length_minus1,
            // .cpb_cnt_minus1[STD_VIDEO_H265_SUBLAYERS_LIST_SIZE] = 0,
            // .elemental_duration_in_tc_minus1[STD_VIDEO_H265_SUBLAYERS_LIST_SIZE] = 0,
            .pSubLayerHrdParametersNal = nullptr, //FIXME
            .pSubLayerHrdParametersVcl = nullptr, //FIXME
        };
      }

      vkp->vui = StdVideoH265SequenceParameterSetVui {
        .flags = StdVideoH265SpsVuiFlags {
          .aspect_ratio_info_present_flag = sps->vui_params.aspect_ratio_info_present_flag,
          .overscan_info_present_flag = sps->vui_params.overscan_info_present_flag,
          .overscan_appropriate_flag = sps->vui_params.overscan_appropriate_flag,
          .video_signal_type_present_flag = sps->vui_params.video_signal_type_present_flag,
          .video_full_range_flag = sps->vui_params.video_full_range_flag,
          .colour_description_present_flag = sps->vui_params.colour_description_present_flag,
          .chroma_loc_info_present_flag = sps->vui_params.chroma_loc_info_present_flag,
          .neutral_chroma_indication_flag = sps->vui_params.neutral_chroma_indication_flag,
          .field_seq_flag = sps->vui_params.field_seq_flag,
          .frame_field_info_present_flag = sps->vui_params.frame_field_info_present_flag,
          .default_display_window_flag = sps->vui_params.default_display_window_flag,
          .vui_timing_info_present_flag = sps->vui_params.timing_info_present_flag,
          .vui_poc_proportional_to_timing_flag = sps->vui_params.poc_proportional_to_timing_flag,
          .vui_hrd_parameters_present_flag = sps->vui_params.hrd_parameters_present_flag,
          .bitstream_restriction_flag = sps->vui_params.bitstream_restriction_flag,
          .tiles_fixed_structure_flag = sps->vui_params.tiles_fixed_structure_flag,
          .motion_vectors_over_pic_boundaries_flag = sps->vui_params.motion_vectors_over_pic_boundaries_flag,
          .restricted_ref_pic_lists_flag = sps->vui_params.restricted_ref_pic_lists_flag,
        },
        .aspect_ratio_idc = static_cast<StdVideoH265AspectRatioIdc>(sps->vui_params.aspect_ratio_idc),
        .sar_width = sps->vui_params.sar_width, //FIXME: 1 with NVidia parser
        .sar_height = sps->vui_params.sar_height, //FIXME: 1 with NVidia parser
        .video_format = sps->vui_params.video_format,
        .colour_primaries = sps->vui_params.colour_primaries,
        .transfer_characteristics = sps->vui_params.transfer_characteristics,
        .matrix_coeffs = sps->vui_params.matrix_coefficients,
        .chroma_sample_loc_type_top_field = sps->vui_params.chroma_sample_loc_type_top_field,
        .chroma_sample_loc_type_bottom_field = sps->vui_params.chroma_sample_loc_type_bottom_field,
        .def_disp_win_left_offset = static_cast<uint16_t>(sps->vui_params.def_disp_win_left_offset),
        .def_disp_win_right_offset = static_cast<uint16_t>(sps->vui_params.def_disp_win_right_offset),
        .def_disp_win_top_offset = static_cast<uint16_t>(sps->vui_params.def_disp_win_top_offset),
        .def_disp_win_bottom_offset = static_cast<uint16_t>(sps->vui_params.def_disp_win_bottom_offset),
        .vui_num_units_in_tick = sps->vui_params.num_units_in_tick,
        .vui_time_scale = sps->vui_params.time_scale, //FIXME: 0 with NVidia parser
        .vui_num_ticks_poc_diff_one_minus1 = sps->vui_params.num_ticks_poc_diff_one_minus1,
        .min_spatial_segmentation_idc = sps->vui_params.min_spatial_segmentation_idc,
        .max_bytes_per_pic_denom = sps->vui_params.max_bytes_per_pic_denom,
        .max_bits_per_min_cu_denom = sps->vui_params.max_bits_per_min_cu_denom,
        .log2_max_mv_length_horizontal = sps->vui_params.log2_max_mv_length_horizontal,
        .log2_max_mv_length_vertical = sps->vui_params.log2_max_mv_length_vertical,
        .pHrdParameters = &vkp->hrd,
      };
    }

    vkp->profileTierLevel =  StdVideoH265ProfileTierLevel {
      .flags = StdVideoH265ProfileTierLevelFlags {
          .general_tier_flag = sps->profile_tier_level.tier_flag,
          .general_progressive_source_flag = sps->profile_tier_level.progressive_source_flag,
          .general_interlaced_source_flag = sps->profile_tier_level.interlaced_source_flag,
          .general_non_packed_constraint_flag = sps->profile_tier_level.non_packed_constraint_flag,
          .general_frame_only_constraint_flag = sps->profile_tier_level.frame_only_constraint_flag,
      },
      .general_profile_idc = get_profile_idc(static_cast<GstH265ProfileIDC>(sps->profile_tier_level.profile_idc)),
      .general_level_idc = static_cast<StdVideoH265LevelIdc>(sps->profile_tier_level.level_idc),
    };

    fill_scaling_list (&sps->scaling_list, &vkp->scaling_lists_sps);

    vkp->sps = StdVideoH265SequenceParameterSet {
        .flags = {
            .sps_temporal_id_nesting_flag = sps->temporal_id_nesting_flag,
            .separate_colour_plane_flag = sps->separate_colour_plane_flag,
            .conformance_window_flag = sps->conformance_window_flag,
            .sps_sub_layer_ordering_info_present_flag = sps->sub_layer_ordering_info_present_flag,
            .scaling_list_enabled_flag = sps->scaling_list_enabled_flag,
            .sps_scaling_list_data_present_flag = sps->scaling_list_data_present_flag,
            .amp_enabled_flag = sps->amp_enabled_flag,
            .sample_adaptive_offset_enabled_flag = sps->sample_adaptive_offset_enabled_flag,
            .pcm_enabled_flag = sps->pcm_enabled_flag,
            .pcm_loop_filter_disabled_flag = sps->pcm_loop_filter_disabled_flag,
            .long_term_ref_pics_present_flag = sps->long_term_ref_pics_present_flag,
            .sps_temporal_mvp_enabled_flag = sps->temporal_mvp_enabled_flag,
            .strong_intra_smoothing_enabled_flag = sps->strong_intra_smoothing_enabled_flag,
            .vui_parameters_present_flag = sps->vui_parameters_present_flag,
            .sps_extension_present_flag = sps->sps_extension_flag,
            .sps_range_extension_flag = sps->sps_range_extension_flag,
            .transform_skip_rotation_enabled_flag = 0,//Filled if sps_extension_flag
            .transform_skip_context_enabled_flag = 0,//Filled if sps_extension_flag
            .implicit_rdpcm_enabled_flag = 0,//Filled if sps_extension_flag
            .explicit_rdpcm_enabled_flag = 0,//Filled if sps_extension_flag
            .extended_precision_processing_flag = 0,//Filled if sps_extension_flag
            .intra_smoothing_disabled_flag = 0,//Filled if sps_extension_flag
            .high_precision_offsets_enabled_flag = 0,//Filled if sps_extension_flag
            .persistent_rice_adaptation_enabled_flag = 0,//Filled if sps_extension_flag
            .cabac_bypass_alignment_enabled_flag = 0,//Filled if sps_extension_flag
            .sps_scc_extension_flag = sps->sps_scc_extension_flag,
            .sps_curr_pic_ref_enabled_flag = sps->sps_scc_extension_params.sps_curr_pic_ref_enabled_flag,
            .palette_mode_enabled_flag = sps->sps_scc_extension_params.palette_mode_enabled_flag,
            .sps_palette_predictor_initializers_present_flag = sps->sps_scc_extension_params.sps_palette_predictor_initializers_present_flag,
            .intra_boundary_filtering_disabled_flag = sps->sps_scc_extension_params.intra_boundary_filtering_disabled_flag,
        },
        .chroma_format_idc = static_cast<StdVideoH265ChromaFormatIdc>(sps->chroma_format_idc),
        .pic_width_in_luma_samples = sps->pic_width_in_luma_samples,
        .pic_height_in_luma_samples = sps->pic_height_in_luma_samples,
#if GST_CHECK_VERSION(1, 21, 1)
        .sps_video_parameter_set_id = sps->vps_id,
#else
        .sps_video_parameter_set_id = sps->vps ? sps->vps->id : (uint8_t)0,
#endif        
        .sps_max_sub_layers_minus1 = sps->max_sub_layers_minus1,
        .sps_seq_parameter_set_id = sps->id,
        .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
        .log2_min_luma_coding_block_size_minus3 = sps->log2_min_luma_coding_block_size_minus3,
        .log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_luma_coding_block_size,
        .log2_min_luma_transform_block_size_minus2 = sps->log2_min_transform_block_size_minus2,
        .log2_diff_max_min_luma_transform_block_size = sps->log2_diff_max_min_transform_block_size,
        .max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra,
        .num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets,
        .num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps,
        .pcm_sample_bit_depth_luma_minus1 = sps->pcm_sample_bit_depth_luma_minus1,
        .pcm_sample_bit_depth_chroma_minus1 = sps->pcm_sample_bit_depth_chroma_minus1,
        .log2_min_pcm_luma_coding_block_size_minus3 = sps->log2_min_pcm_luma_coding_block_size_minus3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->log2_diff_max_min_pcm_luma_coding_block_size,
        .palette_max_size = 0, // Filled if sps_scc_extension_flag, see above
        .delta_palette_max_predictor_size = 0,// Filled if sps_scc_extension_flag, see above
        .motion_vector_resolution_control_idc = 0,// Filled if sps_scc_extension_flag, see above
        .sps_num_palette_predictor_initializers_minus1 = 0, // Filled if sps_scc_extension_flag, see above
        .conf_win_left_offset = sps->conf_win_left_offset,
        .conf_win_right_offset = sps->conf_win_right_offset,
        .conf_win_top_offset = sps->conf_win_top_offset,
        .conf_win_bottom_offset = sps->conf_win_bottom_offset,
        .pProfileTierLevel = &vkp->profileTierLevel,
        .pDecPicBufMgr = &vkp->pic_buf_mgr, // FIXME: Not available in the NVidia parser
        .pScalingLists = sps->scaling_list_enabled_flag ? &vkp->scaling_lists_sps : nullptr,
        .pShortTermRefPicSet = nullptr, //FIXME
        .pLongTermRefPicsSps = nullptr, //FIXME
        .pSequenceParameterSetVui = &vkp->vui,
        .pPredictorPaletteEntries = nullptr, //FIXME
    };

    if (sps->vps) {
      vkp->sps.sps_video_parameter_set_id = sps->vps->id;
    }
#if !GST_CHECK_VERSION (1,21,0)
    # define sps_extension_params sps_extnsion_params
#endif
    if (sps->sps_extension_flag) {
      vkp->sps.flags.transform_skip_rotation_enabled_flag = sps->sps_extension_params.transform_skip_context_enabled_flag;
      vkp->sps.flags.transform_skip_context_enabled_flag = sps->sps_extension_params.transform_skip_context_enabled_flag;
      vkp->sps.flags.implicit_rdpcm_enabled_flag = sps->sps_extension_params.implicit_rdpcm_enabled_flag;
      vkp->sps.flags.explicit_rdpcm_enabled_flag = sps->sps_extension_params.explicit_rdpcm_enabled_flag;
      vkp->sps.flags.extended_precision_processing_flag = sps->sps_extension_params.extended_precision_processing_flag;
      vkp->sps.flags.intra_smoothing_disabled_flag = sps->sps_extension_params.intra_smoothing_disabled_flag;
      vkp->sps.flags.high_precision_offsets_enabled_flag = sps->sps_extension_params.high_precision_offsets_enabled_flag;
      vkp->sps.flags.persistent_rice_adaptation_enabled_flag = sps->sps_extension_params.persistent_rice_adaptation_enabled_flag;
      vkp->sps.flags.cabac_bypass_alignment_enabled_flag = sps->sps_extension_params.cabac_bypass_alignment_enabled_flag;
    }

    if (sps->sps_scc_extension_flag) {
      vkp->sps.palette_max_size = sps->sps_scc_extension_params.palette_max_size;
      vkp->sps.delta_palette_max_predictor_size = sps->sps_scc_extension_params.delta_palette_max_predictor_size;
      vkp->sps.motion_vector_resolution_control_idc = sps->sps_scc_extension_params.motion_vector_resolution_control_idc;
      vkp->sps.sps_num_palette_predictor_initializers_minus1 = sps->sps_scc_extension_params.sps_num_palette_predictor_initializer_minus1;
    }
}

static void
fill_pps (GstH265PPS * pps, VkH265Picture * vkp)
{

  fill_scaling_list(&pps->scaling_list, &vkp->scaling_lists_pps);

  vkp->pps = StdVideoH265PictureParameterSet {
    .flags = {
      .dependent_slice_segments_enabled_flag = pps->dependent_slice_segments_enabled_flag,
      .output_flag_present_flag = pps->output_flag_present_flag,
      .sign_data_hiding_enabled_flag = pps->sign_data_hiding_enabled_flag,
      .cabac_init_present_flag = pps->cabac_init_present_flag,
      .constrained_intra_pred_flag = pps->constrained_intra_pred_flag,
      .transform_skip_enabled_flag = pps->transform_skip_enabled_flag,
      .cu_qp_delta_enabled_flag = pps->cu_qp_delta_enabled_flag,
      .pps_slice_chroma_qp_offsets_present_flag = pps->slice_chroma_qp_offsets_present_flag,
      .weighted_pred_flag = pps->weighted_pred_flag,
      .weighted_bipred_flag = pps->weighted_bipred_flag,
      .transquant_bypass_enabled_flag = pps->transquant_bypass_enabled_flag,
      .tiles_enabled_flag = pps->tiles_enabled_flag,
      .entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag,
      .uniform_spacing_flag = pps->uniform_spacing_flag,
      .loop_filter_across_tiles_enabled_flag = pps->loop_filter_across_tiles_enabled_flag,
      .pps_loop_filter_across_slices_enabled_flag = pps->loop_filter_across_slices_enabled_flag,
      .deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag,
      .deblocking_filter_override_enabled_flag = pps->deblocking_filter_override_enabled_flag,
      .pps_deblocking_filter_disabled_flag = pps->deblocking_filter_disabled_flag,
      .pps_scaling_list_data_present_flag = pps->scaling_list_data_present_flag,
      .lists_modification_present_flag = pps->lists_modification_present_flag,
      .slice_segment_header_extension_present_flag = pps->slice_segment_header_extension_present_flag,
      .pps_extension_present_flag = pps->pps_extension_flag,
      .cross_component_prediction_enabled_flag = pps->pps_extension_params.cross_component_prediction_enabled_flag,
      .chroma_qp_offset_list_enabled_flag = pps->pps_extension_params.chroma_qp_offset_list_enabled_flag,
      .pps_curr_pic_ref_enabled_flag = pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag,
      .residual_adaptive_colour_transform_enabled_flag = pps->pps_scc_extension_params.residual_adaptive_colour_transform_enabled_flag,
      .pps_slice_act_qp_offsets_present_flag = pps->pps_scc_extension_params.pps_slice_act_qp_offsets_present_flag,
      .pps_palette_predictor_initializers_present_flag = pps->pps_scc_extension_params.pps_palette_predictor_initializers_present_flag,
      .monochrome_palette_flag = pps->pps_scc_extension_params.monochrome_palette_flag,
      .pps_range_extension_flag = pps->pps_range_extension_flag,
    },
    .pps_pic_parameter_set_id = static_cast<uint8_t>(pps->id),
#if GST_CHECK_VERSION(1, 21, 1)
    .pps_seq_parameter_set_id = static_cast<uint8_t>(pps->sps_id), //https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/2575
#else
    .pps_seq_parameter_set_id = 0, //FIXME
#endif
    //.sps_video_parameter_set_id = pps->vps_id,
    .num_extra_slice_header_bits = pps->num_extra_slice_header_bits,
    .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1,
    .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1,
    .init_qp_minus26 = pps->init_qp_minus26,
    .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
    .pps_cb_qp_offset = pps->cb_qp_offset,
    .pps_cr_qp_offset = pps->cr_qp_offset,
    .pps_beta_offset_div2 = pps->beta_offset_div2,
    .pps_tc_offset_div2 = pps->tc_offset_div2,
    .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2,
    .log2_max_transform_skip_block_size_minus2 = static_cast<uint8_t>(pps->pps_extension_params.log2_max_transform_skip_block_size_minus2),
    .diff_cu_chroma_qp_offset_depth = pps->pps_extension_params.diff_cu_chroma_qp_offset_depth,
    .chroma_qp_offset_list_len_minus1 = pps->pps_extension_params.chroma_qp_offset_list_len_minus1,
    //.cb_qp_offset_list = pps->cb_qp_offset, // memcpy above
    //.cr_qp_offset_list = pps->cr_qp_offset, // memcpy above
    .log2_sao_offset_scale_luma = pps->pps_extension_params.log2_sao_offset_scale_luma,
    .log2_sao_offset_scale_chroma = pps->pps_extension_params.log2_sao_offset_scale_chroma,
    .pps_act_y_qp_offset_plus5 = static_cast<int8_t>(pps->pps_scc_extension_params.pps_act_y_qp_offset_plus5),
    .pps_act_cb_qp_offset_plus5 = static_cast<int8_t>(pps->pps_scc_extension_params.pps_act_cb_qp_offset_plus5),
    .pps_act_cr_qp_offset_plus3 = static_cast<int8_t>(pps->pps_scc_extension_params.pps_act_cr_qp_offset_plus3),
    .pps_num_palette_predictor_initializers = pps->pps_scc_extension_params.pps_num_palette_predictor_initializer,
    .luma_bit_depth_entry_minus8 = pps->pps_scc_extension_params.luma_bit_depth_entry_minus8,
    .chroma_bit_depth_entry_minus8 = static_cast<uint8_t>(pps->pps_scc_extension_params.chroma_bit_depth_entry_minus8),
    .num_tile_columns_minus1 = pps->num_tile_columns_minus1,
    .num_tile_rows_minus1 = pps->num_tile_rows_minus1,
    //.column_width_minus1 = 0,// memcpy above
    //.row_height_minus1 = 0,// memcpy above
    .pScalingLists =  pps->scaling_list_data_present_flag ? &vkp->scaling_lists_pps : nullptr,
    .pPredictorPaletteEntries = nullptr,
  };

  //memcpy(vkp->pps.cb_qp_offset_list, pps->cb_qp_offset, sizeof(vkp->pps.cb_qp_offset_list)); //STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE
  //memcpy(vkp->pps.cr_qp_offset_list, pps->cr_qp_offset, sizeof(vkp->pps.cr_qp_offset_list)); //STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_ROWS_LIST_SIZE
  memcpy(vkp->pps.column_width_minus1, pps->column_width_minus1, sizeof(vkp->pps.column_width_minus1)); //STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE
  memcpy(vkp->pps.row_height_minus1, pps->row_height_minus1, sizeof(vkp->pps.row_height_minus1)); 
}

static void
fill_vps (GstH265VPS * vps, VkH265Picture * vkp)
{
  vkp->vps = StdVideoH265VideoParameterSet {
    .flags = {
      .vps_temporal_id_nesting_flag = vps->temporal_id_nesting_flag,
      .vps_sub_layer_ordering_info_present_flag = vps->sub_layer_ordering_info_present_flag,
      .vps_timing_info_present_flag = vps->timing_info_present_flag,
      .vps_poc_proportional_to_timing_flag = vps->poc_proportional_to_timing_flag,
    },
    .vps_video_parameter_set_id = vps->id,
    .vps_max_sub_layers_minus1 = vps->max_sub_layers_minus1,
    .vps_num_units_in_tick = vps->num_units_in_tick,
    .vps_time_scale = vps->time_scale,
    .vps_num_ticks_poc_diff_one_minus1 = vps->num_ticks_poc_diff_one_minus1,
    // const StdVideoH265HrdParameters*    pHrdParameters;
  };

  memcpy (vkp->pic_buf_mgr.max_latency_increase_plus1, vps->max_latency_increase_plus1, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE);
  memcpy (vkp->pic_buf_mgr.max_dec_pic_buffering_minus1, vps->max_dec_pic_buffering_minus1, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE);
  memcpy (vkp->pic_buf_mgr.max_num_reorder_pics, vps->max_num_reorder_pics, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE);
  vkp->vps.pDecPicBufMgr = &vkp->pic_buf_mgr;
}

static GstFlowReturn
gst_vk_h265_dec_start_picture (GstH265Decoder * decoder, GstH265Picture * picture,
    GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  VkPic *vkpic =  gst_vk_h265_dec_get_decoder_frame_from_picture(decoder, picture);
  VkH265Picture *vkp = &self->vkp;
  GstH265PPS *pps = slice->header.pps;
  GstH265SPS *sps = pps->sps;
  GstH265VPS *vps = sps->vps;

  if (!self->oob_pic_params
      || (self->sps_update_count == 0 && self->sps_update_count == 0)) {
    vkp = &vkpic->vkp;
    fill_sps (sps, vkp);
    fill_pps (pps, vkp);
  }
  // Following bad/sys/nvcodec/gstnvh265dec.c
  if (pps->scaling_list_data_present_flag ||
      (sps->scaling_list_enabled_flag
          && !sps->scaling_list_data_present_flag)) {
      fill_scaling_list (&pps->scaling_list, &vkp->scaling_lists_sps);
      vkp->sps.pScalingLists  = &vkp->scaling_lists_sps;
    }

  vkpic->data = VkParserPictureData {
      .PicWidthInMbs = sps->width / 16, // Coded Frame Size
      .FrameHeightInMbs = sps->height / 16, // Coded Frame Height
      .pCurrPic = vkpic->pic,
      .field_pic_flag = sps->vui_parameters_present_flag ? sps->vui_params.field_seq_flag : 0, // 0=frame picture, 1=field picture
      .bottom_field_flag = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD) != 0, // 0=top field, 1=bottom field (ignored if field_pic_flag=0)
      //.second_field = , // Second field of a complementary field pair
      .progressive_frame = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) == 0, // Frame is progressive
      .top_field_first = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0,
      .repeat_first_field = 0, // For 3:2 pulldown (number of additional fields,
      //                          // 2=frame doubling, 4=frame tripling)
      .ref_pic_flag = picture->ref, // Frame is a reference frame
      .intra_pic_flag = picture->IntraPicFlag,
      .chroma_format = sps->chroma_format_idc, // Chroma Format (should match sequence info)
      .picture_order_count = picture->pic_order_cnt, // picture order count (if known) //FIXME: is multiplied by 2 in NVidia parser

      .pbSideData = nullptr, // Encryption Info
      .nSideDataLen = 0, // Encryption Info length

      // Bitstream data
      //.nBitstreamDataLen, // Number of bytes in bitstream data buffer
      //.pBitstreamData, // Ptr to bitstream data for this picture (slice-layer)
      //.pSliceDataOffsets, // nNumSlices entries, contains offset of each slice
      // within the bitstream data buffer
  };

  VkParserHevcPictureData *h265 = &vkpic->data.CodecSpecific.hevc;
  *h265 = VkParserHevcPictureData {
      .pStdVps = &vkp->vps,
      .pVpsClientObject = self->vpsclient,
      .pStdSps = &vkp->sps,
      .pSpsClientObject = self->spsclient,
      .pStdPps = &vkp->pps,
      .pPpsClientObject = self->ppsclient,
      .pic_parameter_set_id = static_cast<uint8_t>(pps->id), // PPS ID
      .seq_parameter_set_id = static_cast<uint8_t>(sps->id), // SPS ID
      .vps_video_parameter_set_id = static_cast<uint8_t>(vps->id), // VPS ID

      .IrapPicFlag = GST_H265_IS_NAL_TYPE_IRAP(slice->nalu.type),
      .IdrPicFlag = GST_H265_IS_NAL_TYPE_IDR(slice->nalu.type),

      // // RefPicSets
      .NumBitsForShortTermRPSInSlice = (int32_t)slice->header.short_term_ref_pic_set_size,
      .NumDeltaPocsOfRefRpsIdx = slice->header.short_term_ref_pic_sets.NumDeltaPocsOfRefRpsIdx,
      .NumPocTotalCurr = slice->header.NumPocTotalCurr,
      .NumPocStCurrBefore = (int32_t)decoder->NumPocStCurrBefore,
      .NumPocStCurrAfter = (int32_t)decoder->NumPocStCurrAfter,
      .NumPocLtCurr = (int32_t)decoder->NumPocLtCurr,
      .CurrPicOrderCntVal = picture->pic_order_cnt,


      // int8_t RefPicSetStCurrBefore[8];
      // int8_t RefPicSetStCurrAfter[8];
      // int8_t RefPicSetLtCurr[8];

      // // various profile related
      // // 0 = invalid, 1 = Main, 2 = Main10, 3 = still picture, 4 = Main 12,
      // // 5 = MV-HEVC Main8
      .ProfileLevel = vps->profile_tier_level.profile_idc,
      .ColorPrimaries = (sps->vui_parameters_present_flag ? sps->vui_params.colour_primaries : (uint8_t)0), // ColorPrimariesBTXXXX enum
      .bit_depth_luma_minus8 = (pps->pps_scc_extension_flag ? pps->pps_scc_extension_params.luma_bit_depth_entry_minus8 : (uint8_t)0),
      .bit_depth_chroma_minus8 = (uint8_t)(pps->pps_scc_extension_flag ? pps->pps_scc_extension_params.chroma_bit_depth_entry_minus8 : 0),

      // // MV-HEVC related fields
      // uint8_t mv_hevc_enable;
      // uint8_t nuh_layer_id;
      // uint8_t default_ref_layers_active_flag;
      // uint8_t NumDirectRefLayers;
      // uint8_t max_one_active_ref_layer_flag;
      // uint8_t poc_lsb_not_present_flag;
      // uint8_t pad0[2];

      // int32_t NumActiveRefLayerPics0;
      // int32_t NumActiveRefLayerPics1;
      // int8_t RefPicSetInterLayer0[8];
      // int8_t RefPicSetInterLayer1[8];
  };

  /* reference frames */
  GArray * dpb_array = gst_h265_dpb_get_pictures_all(dpb);
  guint i, j, k;
  guint num_ref_pic = 0;
  for (i = 0; i < dpb_array->len; i++) {
    GstH265Picture *other = g_array_index (dpb_array, GstH265Picture *, i);
    VkPic *other_frame;
    //gint picture_index = -1;

    if (!other->ref)
      continue;

    if (num_ref_pic >= G_N_ELEMENTS (h265->RefPics)) {
      GST_ERROR_OBJECT (self, "Too many reference frames");
      return GST_FLOW_ERROR;
    }

    other_frame = gst_vk_h265_dec_get_decoder_frame_from_picture (decoder, other);

    h265->RefPics[num_ref_pic] = other_frame->pic;
    h265->PicOrderCntVal[num_ref_pic] = other->pic_order_cnt;
    h265->IsLongTerm[num_ref_pic] = other->long_term;

    num_ref_pic++;
  }
  g_array_unref (dpb_array);
    for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocStCurrBefore)
      other = decoder->RefPicSetStCurrBefore[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265->RefPicSetStCurrBefore[i] = k;
          break;
        }
      }
    }
  }

  for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocStCurrAfter)
      other = decoder->RefPicSetStCurrAfter[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265->RefPicSetStCurrAfter[i] = k;
          break;
        }
      }
    }
  }

  for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocLtCurr)
      other = decoder->RefPicSetLtCurr[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265->RefPicSetLtCurr[i] = k;
          break;
        }
      }
    }
  }

  return GST_FLOW_OK;
}

static void
gst_vk_h265_dec_unhandled_nalu (GstH265Decoder * decoder, const guint8 * data,
    guint32 size)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);

  if (self->client)
    self->client->UnhandledNALU (data, size);
}

static bool
sps_cmp (GstH265SPS * a, GstH265SPS * b)
{
  int i;
#define CMP_FIELD(x) G_STMT_START { if (a->x != b->x) return false; } G_STMT_END
  CMP_FIELD (id);

  //CMP_FIELD (vps_id);
  //GstH265VPS *vps;

  CMP_FIELD (max_sub_layers_minus1);
  CMP_FIELD (temporal_id_nesting_flag);

  //GstH265ProfileTierLevel profile_tier_level;

  CMP_FIELD (chroma_format_idc);
  CMP_FIELD (separate_colour_plane_flag);
  CMP_FIELD (pic_width_in_luma_samples);
  CMP_FIELD (pic_height_in_luma_samples);

  CMP_FIELD (conformance_window_flag);
  /* if conformance_window_flag */
  CMP_FIELD (conf_win_left_offset);
  CMP_FIELD (conf_win_right_offset);
  CMP_FIELD (conf_win_top_offset);
  CMP_FIELD (conf_win_bottom_offset);

  CMP_FIELD (bit_depth_luma_minus8);
  CMP_FIELD (bit_depth_chroma_minus8);
  CMP_FIELD (log2_max_pic_order_cnt_lsb_minus4);

  CMP_FIELD (sub_layer_ordering_info_present_flag);
  for (i = 0; i < GST_H265_MAX_SUB_LAYERS; i++) {
    CMP_FIELD (max_dec_pic_buffering_minus1[i]);
    CMP_FIELD (max_num_reorder_pics[i]);
    CMP_FIELD (max_latency_increase_plus1[i]);
  }
  CMP_FIELD (log2_min_luma_coding_block_size_minus3);
  CMP_FIELD (log2_diff_max_min_luma_coding_block_size);
  CMP_FIELD (log2_min_transform_block_size_minus2);
  CMP_FIELD (log2_diff_max_min_transform_block_size);
  CMP_FIELD (max_transform_hierarchy_depth_inter);
  CMP_FIELD (max_transform_hierarchy_depth_intra);

  CMP_FIELD (scaling_list_enabled_flag);
  /* if scaling_list_enabled_flag */
  CMP_FIELD (scaling_list_data_present_flag);

  // GstH265ScalingList scaling_list;

  CMP_FIELD (amp_enabled_flag);
  CMP_FIELD (sample_adaptive_offset_enabled_flag);
  CMP_FIELD (pcm_enabled_flag);
  /* if pcm_enabled_flag */
  CMP_FIELD (pcm_sample_bit_depth_luma_minus1);
  CMP_FIELD (pcm_sample_bit_depth_chroma_minus1);
  CMP_FIELD (log2_min_pcm_luma_coding_block_size_minus3);
  CMP_FIELD (log2_diff_max_min_pcm_luma_coding_block_size);
  CMP_FIELD (pcm_loop_filter_disabled_flag);

  CMP_FIELD (num_short_term_ref_pic_sets);
  // GstH265ShortTermRefPicSet short_term_ref_pic_set[65];

  CMP_FIELD (long_term_ref_pics_present_flag);
  /* if long_term_ref_pics_present_flag */
  CMP_FIELD (num_long_term_ref_pics_sps);
  // CMP_FIELD (lt_ref_pic_poc_lsb_sps[32]);
  // CMP_FIELD (used_by_curr_pic_lt_sps_flag[32]);

  CMP_FIELD (temporal_mvp_enabled_flag);
  CMP_FIELD (strong_intra_smoothing_enabled_flag);
  CMP_FIELD (vui_parameters_present_flag);

  /* if vui_parameters_present_flat */
  // GstH265VUIParams vui_params;

  CMP_FIELD (sps_extension_flag);

  /* if sps_extension_present_flag */
  CMP_FIELD (sps_range_extension_flag);
  CMP_FIELD (sps_multilayer_extension_flag);
  CMP_FIELD (sps_3d_extension_flag);
  CMP_FIELD (sps_scc_extension_flag);
  CMP_FIELD (sps_extension_4bits);

  /* if sps_range_extension_flag */
  // GstH265SPSExtensionParams sps_extension_params;
  /* if sps_scc_extension_flag */
  // GstH265SPSSccExtensionParams sps_scc_extension_params;

  /* calculated values */
  CMP_FIELD (chroma_array_type);
  CMP_FIELD (width);
  CMP_FIELD (height);
  CMP_FIELD (crop_rect_width);
  CMP_FIELD (crop_rect_height);
  CMP_FIELD (crop_rect_x);
  CMP_FIELD (crop_rect_y);
  CMP_FIELD (fps_num);
  CMP_FIELD (fps_den);
  CMP_FIELD (valid);

  return true;
#undef CMP_FIELD
}

static bool
pps_cmp (GstH265PPS * a, GstH265PPS * b)
{
#define CMP_FIELD(x)  G_STMT_START { if (a->x != b->x) return false;  }  G_STMT_END
  CMP_FIELD (id);

  //CMP_FIELD (sps_id);
  // GstH265SPS *sps;

  CMP_FIELD (dependent_slice_segments_enabled_flag);
  CMP_FIELD (output_flag_present_flag);
  CMP_FIELD (num_extra_slice_header_bits);
  CMP_FIELD (sign_data_hiding_enabled_flag);
  CMP_FIELD (cabac_init_present_flag);
  CMP_FIELD (num_ref_idx_l0_default_active_minus1);
  CMP_FIELD (num_ref_idx_l1_default_active_minus1);
  CMP_FIELD (init_qp_minus26);
  CMP_FIELD (constrained_intra_pred_flag);
  CMP_FIELD (transform_skip_enabled_flag);
  CMP_FIELD (cu_qp_delta_enabled_flag);
  /*if cu_qp_delta_enabled_flag */
  CMP_FIELD (diff_cu_qp_delta_depth);

  CMP_FIELD (cb_qp_offset);
  CMP_FIELD (cr_qp_offset);
  CMP_FIELD (slice_chroma_qp_offsets_present_flag);
  CMP_FIELD (weighted_pred_flag);
  CMP_FIELD (weighted_bipred_flag);
  CMP_FIELD (transquant_bypass_enabled_flag);
  CMP_FIELD (tiles_enabled_flag);
  CMP_FIELD (entropy_coding_sync_enabled_flag);

  CMP_FIELD (num_tile_columns_minus1);
  CMP_FIELD (num_tile_rows_minus1);
  CMP_FIELD (uniform_spacing_flag);
  // CMP_FIELD (column_width_minus1[20]);
  // CMP_FIELD (row_height_minus1[22]);
  CMP_FIELD (loop_filter_across_tiles_enabled_flag);

  CMP_FIELD (loop_filter_across_slices_enabled_flag);
  CMP_FIELD (deblocking_filter_control_present_flag);
  CMP_FIELD (deblocking_filter_override_enabled_flag);
  CMP_FIELD (deblocking_filter_disabled_flag);
  CMP_FIELD (beta_offset_div2);
  CMP_FIELD (tc_offset_div2);

  CMP_FIELD (scaling_list_data_present_flag);

  // GstH265ScalingList scaling_list;

  CMP_FIELD (lists_modification_present_flag);
  CMP_FIELD (log2_parallel_merge_level_minus2);
  CMP_FIELD (slice_segment_header_extension_present_flag);

  CMP_FIELD (pps_extension_flag);

  /* if pps_extension_flag*/
  CMP_FIELD (pps_range_extension_flag);
  CMP_FIELD (pps_multilayer_extension_flag);
  CMP_FIELD (pps_3d_extension_flag);
  CMP_FIELD (pps_scc_extension_flag);
  CMP_FIELD (pps_extension_4bits);

  /* if pps_range_extension_flag*/
  // GstH265PPSExtensionParams pps_extension_params;
  /* if pps_scc_extension_flag*/
  // GstH265PPSSccExtensionParams pps_scc_extension_params;

  /* calculated values */
  CMP_FIELD (PicWidthInCtbsY);
  CMP_FIELD (PicHeightInCtbsY);
  CMP_FIELD (valid);

  return true;
#undef CMP_FIELD
}

static bool
vps_cmp (GstH265VPS * a, GstH265VPS * b)
{
#define CMP_FIELD(x)  G_STMT_START { if (a->x != b->x) return false;  }  G_STMT_END
  CMP_FIELD (id);
  CMP_FIELD (base_layer_internal_flag);
  CMP_FIELD (base_layer_available_flag);

  CMP_FIELD (max_layers_minus1);
  CMP_FIELD (max_sub_layers_minus1);
  CMP_FIELD (temporal_id_nesting_flag);

  // GstH265ProfileTierLevel profile_tier_level;

  CMP_FIELD (sub_layer_ordering_info_present_flag);
  // CMP_FIELD (max_dec_pic_buffering_minus1[GST_H265_MAX_SUB_LAYERS]);
  // CMP_FIELD (max_num_reorder_pics[GST_H265_MAX_SUB_LAYERS]);
  // CMP_FIELD (max_latency_increase_plus1[GST_H265_MAX_SUB_LAYERS]);

  CMP_FIELD (max_layer_id);
  CMP_FIELD (num_layer_sets_minus1);

  CMP_FIELD (timing_info_present_flag);
  CMP_FIELD (num_units_in_tick);
  CMP_FIELD (time_scale);
  CMP_FIELD (poc_proportional_to_timing_flag);
  CMP_FIELD (num_ticks_poc_diff_one_minus1);

  CMP_FIELD (num_hrd_parameters);

  /* FIXME: following HRD related info should be an array */
  CMP_FIELD (hrd_layer_set_idx);
  CMP_FIELD (cprms_present_flag);
  // GstH265HRDParams hrd_params;

  CMP_FIELD (vps_extension);

  CMP_FIELD (valid);

  return true;
#undef CMP_FIELD
}

static void
gst_vk_h265_dec_update_picture_parameters (GstH265Decoder * decoder,
    GstH265NalUnitType type, const gpointer nalu)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (decoder);
  VkPictureParameters params;

  switch (type) {
    case GST_H265_NAL_SPS:{
      GstH265SPS *sps = static_cast < GstH265SPS * >(nalu);
      if (sps_cmp (&self->last_sps, sps))
        return;
      self->last_sps = *sps;
      fill_sps (sps, &self->vkp);
      params = VkPictureParameters {
        .updateType = VK_PICTURE_PARAMETERS_UPDATE_H265_SPS,
        .pH265Sps = &self->vkp.sps,
        .updateSequenceCount = self->sps_update_count++,
      };
      if (self->client) {
        if (!self->client->UpdatePictureParameters (&params, self->spsclient,
                params.updateSequenceCount))
          GST_ERROR_OBJECT (self, "Failed to update sequence parameters");
      }
      break;
    }
    case GST_H265_NAL_PPS:{
      GstH265PPS *pps = static_cast < GstH265PPS * >(nalu);
      if (pps_cmp (&self->last_pps, pps))
        return;
      self->last_pps = *pps;
      fill_pps (pps, &self->vkp);
      params = VkPictureParameters {
        .updateType = VK_PICTURE_PARAMETERS_UPDATE_H265_PPS,
        .pH265Pps = &self->vkp.pps,
        .updateSequenceCount = self->pps_update_count++,
      };
      if (self->client) {
        if (!self->client->UpdatePictureParameters (&params, self->ppsclient,
                params.updateSequenceCount))
          GST_ERROR_OBJECT (self, "Failed to update picture parameters");
      }
      break;
    }
    case GST_H265_NAL_VPS:{
      GstH265VPS *vps = static_cast < GstH265VPS * >(nalu);
      if (vps_cmp (&self->last_vps, vps))
         return;
      self->last_vps = *vps;

      fill_vps (vps, &self->vkp);
      params = VkPictureParameters {
        .updateType = VK_PICTURE_PARAMETERS_UPDATE_H265_VPS,
        .pH265Vps = &self->vkp.vps,
        .updateSequenceCount = self->pps_update_count++,
      };
      if (self->client) {
        if (!self->client->UpdatePictureParameters (&params, self->vpsclient,
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
gst_vk_h265_dec_dispose (GObject * object)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (object);

  if (self->spsclient)
    self->spsclient->Release ();
  if (self->ppsclient)
    self->ppsclient->Release ();
  if (self->vpsclient)
    self->vpsclient->Release ();

  g_clear_pointer (&self->refs, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_vk_h265_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVkH265Dec *self = GST_VK_H265_DEC (object);

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
gst_vk_h265_dec_class_init (GstVkH265DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstH265DecoderClass *h265decoder_class = GST_H265_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_set_static_metadata (element_class, "Vulkan H265 parser",
      "Filter/Analyzer/Video",
      "Generates Vulkan Video structures for H265 bitstream",
      "Víctor Jáquez <vjaquez@igalia.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gobject_class->dispose = gst_vk_h265_dec_dispose;
  gobject_class->set_property = gst_vk_h265_dec_set_property;

  h265decoder_class->new_sequence = gst_vk_h265_dec_new_sequence;
  h265decoder_class->decode_slice = gst_vk_h265_dec_decode_slice;
  h265decoder_class->new_picture = gst_vk_h265_dec_new_picture;
  h265decoder_class->output_picture = gst_vk_h265_dec_output_picture;
  h265decoder_class->start_picture = gst_vk_h265_dec_start_picture;
  h265decoder_class->end_picture = gst_vk_h265_dec_end_picture;
  h265decoder_class->unhandled_nalu = gst_vk_h265_dec_unhandled_nalu;
  h265decoder_class->update_picture_parameters =
      gst_vk_h265_dec_update_picture_parameters;

  g_object_class_install_property (gobject_class, PROP_USER_DATA,
      g_param_spec_pointer ("user-data", "user-data", "user-data",
          GParamFlags (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));

  g_object_class_install_property (gobject_class, PROP_OOB_PIC_PARAMS,
      g_param_spec_boolean ("oob-pic-params", "oob-pic-params",
          "oop-pic-params", FALSE,
          GParamFlags (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));
}

static void
gst_vk_h265_dec_init (GstVkH265Dec * self)
{
  gst_h265_decoder_set_process_ref_pic_lists (GST_H265_DECODER (self), FALSE);

  self->refs = g_array_sized_new (FALSE, TRUE, sizeof (GstH265Decoder *), 16);
  g_array_set_clear_func (self->refs, (GDestroyNotify) gst_clear_h265_picture);
}
