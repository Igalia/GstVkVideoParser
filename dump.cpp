/* Dump structures
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

#include "dump.h"

#include <stdarg.h>
#include <stdio.h>

static int indent_depth = 0;
static int indent_size = 4;
static bool pretty_print = true;

static void
print_indent (void)
{
  if (!pretty_print)
    return;
  int i, j;
  for (i = 0; i < indent_depth; i++)
    for (j = 0; j < indent_size; j++)
      putchar (' ');
}

static void
print_newline (void)
{
  if (pretty_print)
    printf ("\n");
}

static void
print_tag (const char *tag)
{
  if (tag) {
    printf ("\"%s\":", tag);
    if (pretty_print)
      printf (" ");
  }
}

static void
start_array (const char *tag)
{
  print_indent ();
  print_tag (tag);
  printf ("[");
  print_newline ();
  ++indent_depth;
}

static void
end_array (void)
{
  --indent_depth;
  print_indent ();
  printf ("],");
  print_newline ();
}

static void
start_object (const char *tag)
{
  print_indent ();
  print_tag (tag);
  printf ("{");
  print_newline ();
  ++indent_depth;
}

static void
end_object (void)
{
  --indent_depth;
  print_indent ();
  printf ("},");
  print_newline ();
}

static void
print_boolean (const char *tag, bool value)
{
  print_indent ();
  print_tag (tag);
  printf ("%s,", value ? "true" : "false");
  print_newline ();
}

static void
print_integer (const char *tag, int64_t value)
{
  print_indent ();
  print_tag (tag);
  printf ("%" PRId64 ",", value);
  print_newline ();
}

static void
print_double (const char *tag, double value)
{
  print_indent ();
  print_tag (tag);
  printf ("%lg,", value);
  print_newline ();
}

static void
print_string (const char *tag, const char *format, ...)
{
  print_indent ();
  print_tag (tag);
  printf ("\"");

  va_list args;
  va_start (args, format);
  vfprintf (stdout, format, args);
  va_end (args);

  printf ("\",");
  print_newline ();
}

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static void
print_hex (const char *tag, const uint8_t * buf, unsigned size)
{
  print_indent ();
  print_tag (tag);

  for (unsigned i = 0; i < MIN (20, size); i++)
    printf (" %02x", buf[i]);

  print_newline ();
}

static void
dump_std_video_h264_sps_flags (const StdVideoH264SpsFlags * flags)
{
  start_object ("StdVideoH264SpsFlags");
  print_integer ("constraint_set0_flag", flags->constraint_set0_flag);
  print_integer ("constraint_set1_flag", flags->constraint_set1_flag);
  print_integer ("constraint_set2_flag", flags->constraint_set2_flag);
  print_integer ("constraint_set3_flag", flags->constraint_set3_flag);
  print_integer ("constraint_set4_flag", flags->constraint_set4_flag);
  print_integer ("constraint_set5_flag", flags->constraint_set5_flag);
  print_integer ("direct_8x8_inference_flag", flags->direct_8x8_inference_flag);
  print_integer ("mb_adaptive_frame_field_flag",
      flags->mb_adaptive_frame_field_flag);
  print_integer ("frame_mbs_only_flag", flags->frame_mbs_only_flag);
  print_integer ("delta_pic_order_always_zero_flag",
      flags->delta_pic_order_always_zero_flag);
  print_integer ("separate_colour_plane_flag",
      flags->separate_colour_plane_flag);
  print_integer ("gaps_in_frame_num_value_allowed_flag",
      flags->gaps_in_frame_num_value_allowed_flag);
  print_integer ("qpprime_y_zero_transform_bypass_flag",
      flags->qpprime_y_zero_transform_bypass_flag);
  print_integer ("frame_cropping_flag", flags->frame_cropping_flag);
  print_integer ("seq_scaling_matrix_present_flag",
      flags->seq_scaling_matrix_present_flag);
  print_integer ("vui_parameters_present_flag",
      flags->vui_parameters_present_flag);
  end_object ();
}

static void
dump_std_video_h264_scaling_lists (const StdVideoH264ScalingLists * lists)
{
  start_object ("StdVideoH264ScalingLists");
  print_integer ("scaling_list_present_mask", lists->scaling_list_present_mask);
  print_integer ("use_default_scaling_matrix_mask",
      lists->use_default_scaling_matrix_mask);
  start_array ("ScalingList4x4");
  for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList4x4[i][j]);
  end_array ();
  start_array ("ScalingList8x8");
  for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList8x8[i][j]);
  end_array ();
  end_object ();
}

static void
dump_std_video_h264_sps_vui_flags (const StdVideoH264SpsVuiFlags * flags)
{
  start_object ("StdVideoH264SpsVuiFlags");
  print_integer ("aspect_ratio_info_present_flag",
      flags->aspect_ratio_info_present_flag);
  print_integer ("overscan_info_present_flag",
      flags->overscan_info_present_flag);
  print_integer ("overscan_appropriate_flag", flags->overscan_appropriate_flag);
  print_integer ("video_signal_type_present_flag",
      flags->video_signal_type_present_flag);
  print_integer ("video_full_range_flag", flags->video_full_range_flag);
  print_integer ("color_description_present_flag",
      flags->color_description_present_flag);
  print_integer ("chroma_loc_info_present_flag",
      flags->chroma_loc_info_present_flag);
  print_integer ("timing_info_present_flag", flags->timing_info_present_flag);
  print_integer ("fixed_frame_rate_flag", flags->fixed_frame_rate_flag);
  print_integer ("bitstream_restriction_flag",
      flags->bitstream_restriction_flag);
  print_integer ("nal_hrd_parameters_present_flag",
      flags->nal_hrd_parameters_present_flag);
  print_integer ("vcl_hrd_parameters_present_flag",
      flags->vcl_hrd_parameters_present_flag);
  end_object ();
}

static void
dump_std_video_h264_sps_vui (const StdVideoH264SequenceParameterSetVui * vui)
{
  start_object ("StdVideoH264SequenceParameterSetVui");
  dump_std_video_h264_sps_vui_flags (&vui->flags);
  print_integer ("aspect_ratio_idc", vui->aspect_ratio_idc);
  print_integer ("sar_width", vui->sar_width);
  print_integer ("sar_height", vui->sar_height);
  print_integer ("video_format", vui->video_format);
  print_integer ("color_primaries", vui->color_primaries);
  print_integer ("transfer_characteristics", vui->transfer_characteristics);
  print_integer ("matrix_coefficients", vui->matrix_coefficients);
  print_integer ("num_units_in_tick", vui->num_units_in_tick);
  print_integer ("time_scale", vui->time_scale);
  //const StdVideoH264HrdParameters* pHrdParameters;
  print_integer ("max_num_reorder_frames", vui->max_num_reorder_frames);
  print_integer ("max_dec_frame_buffering", vui->max_dec_frame_buffering);
  end_object ();
}

static void
dump_std_video_h264_sps (const StdVideoH264SequenceParameterSet * pH264Sps)
{
  start_object ("StdVideoH264SequenceParameterSet");
  dump_std_video_h264_sps_flags (&pH264Sps->flags);
  print_integer ("StdVideoH264ProfileIdc", pH264Sps->profile_idc);
  print_integer ("StdVideoH264Level", pH264Sps->level_idc);
  print_integer ("seq_parameter_set_id", pH264Sps->seq_parameter_set_id);
  print_integer ("chroma_format_idc", pH264Sps->chroma_format_idc);
  print_integer ("bit_depth_luma_minus8", pH264Sps->bit_depth_luma_minus8);
  print_integer ("bit_depth_chroma_minus8", pH264Sps->bit_depth_chroma_minus8);
  print_integer ("log2_max_frame_num_minus4",
      pH264Sps->log2_max_frame_num_minus4);
  print_integer ("pic_order_cnt_type", pH264Sps->pic_order_cnt_type);
  print_integer ("log2_max_pic_order_cnt_lsb_minus4",
      pH264Sps->log2_max_pic_order_cnt_lsb_minus4);
  print_integer ("offset_for_non_ref_pic", pH264Sps->offset_for_non_ref_pic);
  print_integer ("offset_for_top_to_bottom_field",
      pH264Sps->offset_for_top_to_bottom_field);
  print_integer ("num_ref_frames_in_pic_order_cnt_cycle",
      pH264Sps->num_ref_frames_in_pic_order_cnt_cycle);
  print_integer ("max_num_ref_frames", pH264Sps->max_num_ref_frames);
  print_integer ("pic_width_in_mbs_minus1", pH264Sps->pic_width_in_mbs_minus1);
  print_integer ("pic_height_in_map_units_minus1",
      pH264Sps->pic_height_in_map_units_minus1);
  print_integer ("frame_crop_left_offset", pH264Sps->frame_crop_left_offset);
  print_integer ("frame_crop_right_offset", pH264Sps->frame_crop_right_offset);
  print_integer ("frame_crop_top_offset", pH264Sps->frame_crop_top_offset);
  print_integer ("frame_crop_bottom_offset",
      pH264Sps->frame_crop_bottom_offset);
  start_array ("pOffsetForRefFrame");
  for (int i = 0; i < pH264Sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
    print_integer (NULL, pH264Sps->pOffsetForRefFrame[i]);
  end_array ();
  if (pH264Sps->pScalingLists)
    dump_std_video_h264_scaling_lists (pH264Sps->pScalingLists);
  if (pH264Sps->pSequenceParameterSetVui)
    dump_std_video_h264_sps_vui (pH264Sps->pSequenceParameterSetVui);
  end_object ();
}

static void
dump_std_video_h264_pps_flags (const StdVideoH264PpsFlags * flags)
{
  start_object ("StdVideoH264PpsFlags");
  print_integer ("transform_8x8_mode_flag", flags->transform_8x8_mode_flag);
  print_integer ("redundant_pic_cnt_present_flag",
      flags->redundant_pic_cnt_present_flag);
  print_integer ("constrained_intra_pred_flag",
      flags->constrained_intra_pred_flag);
  print_integer ("deblocking_filter_control_present_flag",
      flags->deblocking_filter_control_present_flag);
  print_integer ("weighted_bipred_idc_flag", flags->weighted_bipred_idc_flag);
  print_integer ("weighted_pred_flag", flags->weighted_pred_flag);
  print_integer ("pic_order_present_flag", flags->pic_order_present_flag);
  print_integer ("entropy_coding_mode_flag", flags->entropy_coding_mode_flag);
  print_integer ("pic_scaling_matrix_present_flag",
      flags->pic_scaling_matrix_present_flag);
  end_object ();
}

static void
dump_std_video_h264_pps (const StdVideoH264PictureParameterSet * pps)
{
  start_object ("StdVideoH264PictureParameterSet");
  dump_std_video_h264_pps_flags (&pps->flags);
  print_integer ("seq_parameter_set_id", pps->seq_parameter_set_id);
  print_integer ("pic_parameter_set_id", pps->pic_parameter_set_id);
  print_integer ("num_ref_idx_l0_default_active_minus1",
      pps->num_ref_idx_l0_default_active_minus1);
  print_integer ("num_ref_idx_l1_default_active_minus1",
      pps->num_ref_idx_l1_default_active_minus1);
  print_integer ("weighted_bipred_idc", pps->weighted_bipred_idc);
  print_integer ("pic_init_qp_minus26", pps->pic_init_qp_minus26);
  print_integer ("pic_init_qs_minus26", pps->pic_init_qs_minus26);
  print_integer ("chroma_qp_index_offset", pps->chroma_qp_index_offset);
  print_integer ("second_chroma_qp_index_offset",
      pps->second_chroma_qp_index_offset);
  if (pps->pScalingLists)
    dump_std_video_h264_scaling_lists (pps->pScalingLists);
  end_object ();
}

void
dump_picture_parameters (VkPictureParameters * params)
{
  start_object ("VkPictureParameters");

  switch (params->updateType) {
    case VK_PICTURE_PARAMETERS_UPDATE_H264_SPS:
      dump_std_video_h264_sps (params->pH264Sps);
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H264_PPS:
      dump_std_video_h264_pps (params->pH264Pps);
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H265_VPS:
      print_string ("updateType", "%s", "H265 VPS");
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H265_SPS:
      print_string ("updateType", "%s", "H265 SPS");
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H265_PPS:
      print_string ("updateType", "%s", "H265 PPS");
      break;
  }

  print_integer ("updateSequenceCount", params->updateSequenceCount);
  end_object ();
}

void
dump_parser_sequence_info (const VkParserSequenceInfo * info)
{
  start_object ("VkParserSequenceInfo");
  print_integer ("eCodec", info->eCodec);       // Compression Standard
  print_boolean ("isSVC", info->isSVC); // h.264 SVC
  print_integer ("frameRate", info->frameRate); // Frame Rate stored in the bitstream
  print_integer ("bProgSeq", info->bProgSeq);   // Progressive Sequence
  print_integer ("nDisplayWidth", info->nDisplayWidth); // Displayed Horizontal Size
  print_integer ("nDisplayHeight", info->nDisplayHeight);       // Displayed Vertical Size
  print_integer ("nCodedWidth", info->nCodedWidth);     // Coded Picture Width
  print_integer ("nCodedHeight", info->nCodedHeight);   // Coded Picture Height
  print_integer ("nMaxWidth", info->nMaxWidth); // Max width within sequence
  print_integer ("nMaxHeight", info->nMaxHeight);       // Max height within sequence
  print_integer ("nChromaFormat", info->nChromaFormat); // Chroma Format (0=4:0:0, 1=4:2:0, 2=4:2:2, 3=4:4:4)
  print_integer ("uBitDepthLumaMinus8", info->uBitDepthLumaMinus8);     // Luma bit depth (0=8bit)
  print_integer ("uBitDepthChromaMinus8", info->uBitDepthChromaMinus8); // Chroma bit depth (0=8bit)
  print_integer ("uVideoFullRange", info->uVideoFullRange);     // 0=16-235, 1=0-255
  print_integer ("lBitrate", info->lBitrate);   // Video bitrate (bps)
  print_integer ("lDARWidth", info->lDARWidth);
  print_integer ("lDARHeight", info->lDARHeight);       // Display Aspect Ratio = lDARWidth : lDARHeight
  print_integer ("lVideoFormat", info->lVideoFormat);   // Video Format (VideoFormatXXX)
  print_integer ("lColorPrimaries", info->lColorPrimaries);     // Colour Primaries (ColorPrimariesXXX)
  print_integer ("lTransferCharacteristics", info->lTransferCharacteristics);   // Transfer Characteristics
  print_integer ("lMatrixCoefficients", info->lMatrixCoefficients);     // Matrix Coefficients
  print_integer ("cbSequenceHeader", info->cbSequenceHeader);   // Number of bytes in SequenceHeaderData
  print_integer ("nMinNumDecodeSurfaces", info->nMinNumDecodeSurfaces); // Minimum number of decode surfaces for
  // correct decoding
  print_string ("SequenceHeaderData", "%8s", info->SequenceHeaderData);
  // start_array("SequenceHeaderData");
  // for (unsigned i = 0; i < 1024; i++)
  //     print_integer(NULL, info->SequenceHeaderData[i]); // Raw sequence header data (codec-specific)
  // end_array();
  print_string ("pbSideData", "%8s", info->pbSideData); // uint8_t* pbSideData; // Auxiliary encryption information
  print_integer ("cbSideData", info->cbSideData);       // Auxiliary encryption information length
  end_object ();
}

static void
dump_parser_h264_dpb_entry (const VkParserH264DpbEntry * entry)
{
  start_object ("VkParserH264DpbEntry");
  //VkPicIf* pPicBuf; // ptr to reference frame
  print_integer ("FrameIdx", entry->FrameIdx);  // frame_num(short-term) or LongTermFrameIdx(long-term)
  print_integer ("is_long_term", entry->is_long_term);  // 0=short term reference, 1=long term reference
  print_integer ("not_existing", entry->not_existing);  // non-existing reference frame (corresponding PicIdx
  // should be set to -1)
  print_integer ("used_for_reference", entry->used_for_reference);      // 0=unused, 1=top_field, 2=bottom_field,
  // 3=both_fields
  start_array ("FieldOrderCnt");
  for (int i = 0; i < 2; i++)
    print_integer (NULL, entry->FieldOrderCnt[i]);      // field order count of top and bottom fields
  end_array ();
  end_object ();
}

static void
dump_parser_h264_picture_data (const VkParserH264PictureData * data)
{
  start_object ("VkParserH264PictureData");
  dump_std_video_h264_sps (data->pStdSps);
  //VkParserVideoRefCountBase*              pSpsClientObject;
  dump_std_video_h264_pps (data->pStdPps);
  //VkParserVideoRefCountBase*              pPpsClientObject;
  print_integer ("pic_parameter_set_id", data->pic_parameter_set_id);   // PPS ID
  print_integer ("seq_parameter_set_id", data->seq_parameter_set_id);   // SPS ID
  print_integer ("num_ref_idx_l0_active_minus1",
      data->num_ref_idx_l0_active_minus1);
  print_integer ("num_ref_idx_l1_active_minus1",
      data->num_ref_idx_l1_active_minus1);
  print_integer ("weighted_pred_flag", data->weighted_pred_flag);
  print_integer ("weighted_bipred_idc", data->weighted_bipred_idc);
  print_integer ("pic_init_qp_minus26", data->pic_init_qp_minus26);
  print_integer ("redundant_pic_cnt_present_flag",
      data->redundant_pic_cnt_present_flag);
  print_integer ("deblocking_filter_control_present_flag",
      data->deblocking_filter_control_present_flag);
  print_integer ("transform_8x8_mode_flag", data->transform_8x8_mode_flag);
  print_integer ("MbaffFrameFlag", data->MbaffFrameFlag);
  print_integer ("constrained_intra_pred_flag",
      data->constrained_intra_pred_flag);
  print_integer ("entropy_coding_mode_flag", data->entropy_coding_mode_flag);
  print_integer ("pic_order_present_flag", data->pic_order_present_flag);
  print_integer ("chroma_qp_index_offset", data->chroma_qp_index_offset);
  print_integer ("second_chroma_qp_index_offset",
      data->second_chroma_qp_index_offset);
  print_integer ("frame_num", data->frame_num);
  start_array ("CurrFieldOrderCnt");
  for (int i = 0; i < 2; i++)
    print_integer (NULL, data->CurrFieldOrderCnt[i]);
  end_array ();
  print_integer ("fmo_aso_enable", data->fmo_aso_enable);
  print_integer ("num_slice_groups_minus1", data->num_slice_groups_minus1);
  print_integer ("slice_group_map_type", data->slice_group_map_type);
  print_integer ("pic_init_qs_minus26", data->pic_init_qs_minus26);
  print_integer ("slice_group_change_rate_minus1",
      data->slice_group_change_rate_minus1);
  //const uint8_t* pMb2SliceGroupMap;
  // DPB
  start_array ("dpb");
  for (int i = 0; i < 16 + 1; i++)
    dump_parser_h264_dpb_entry (&data->dpb[i]);
  end_array ();
  end_object ();
}

void
dump_parser_picture_data (VkParserPictureData * pic)
{
  start_object ("VkParserPictureData");
  print_integer ("PicWidthInMbs", pic->PicWidthInMbs);  // Coded Frame Size
  print_integer ("FrameHeightInMbs", pic->FrameHeightInMbs);    // Coded Frame Height
  //VkPicIf* pCurrPic; // Current picture (output)
  print_integer ("field_pic_flag", pic->field_pic_flag);        // 0=frame picture, 1=field picture
  print_integer ("bottom_field_flag", pic->bottom_field_flag);  // 0=top field, 1=bottom field (ignored if
  // field_pic_flag=0)
  print_integer ("second_field", pic->second_field);    // Second field of a complementary field pair
  print_integer ("progressive_frame", pic->progressive_frame);  // Frame is progressive
  print_integer ("top_field_first", pic->top_field_first);      // Frame pictures only
  print_integer ("repeat_first_field", pic->repeat_first_field);        // For 3:2 pulldown (number of additional fields,
  // 2=frame doubling, 4=frame tripling)
  print_integer ("ref_pic_flag", pic->ref_pic_flag);    // Frame is a reference frame
  print_integer ("intra_pic_flag", pic->intra_pic_flag);        // Frame is entirely intra coded (no temporal
  // dependencies)
  print_integer ("chroma_format", pic->chroma_format);  // Chroma Format (should match sequence info)
  print_integer ("picture_order_count", pic->picture_order_count);      // picture order count (if known)
  print_string ("pbSideData", "%s", "");        // uint8_t* pbSideData; // Encryption Info
  print_integer ("nSideDataLen", pic->nSideDataLen);    // Encryption Info length

  // Bitstream data
  print_integer ("nBitstreamDataLen", pic->nBitstreamDataLen);  // Number of bytes in bitstream data buffer
  print_integer ("nNumSlices", pic->nNumSlices);        // Number of slices(tiles in case of AV1) in this picture
  for (unsigned i = 0; i < pic->nNumSlices; i++) {
    unsigned siz;
    if (i == pic->nNumSlices - 1)
      siz = sizeof (pic->pBitstreamData) - pic->pSliceDataOffsets[i];
    else
      siz = pic->pSliceDataOffsets[i + 1];
    print_hex ("pBitstreamData", pic->pBitstreamData + pic->pSliceDataOffsets[i], siz); // Ptr to bitstream data for this picture (slice-layer)
  }
  start_array ("pSliceDataOffsets");
  //const uint32_t* pSliceDataOffsets; // nNumSlices entries, contains offset of each slice
  // within the bitstream data buffer
  for (unsigned i = 0; i < pic->nNumSlices; i++)
    print_integer (NULL, pic->pSliceDataOffsets[i]);
  end_array ();

  dump_parser_h264_picture_data (&pic->CodecSpecific.h264);

  end_object ();
}
