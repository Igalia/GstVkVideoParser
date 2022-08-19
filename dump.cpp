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

#if 0
static void
print_double (const char *tag, double value)
{
  print_indent ();
  print_tag (tag);
  printf ("%lg,", value);
  print_newline ();
}
#endif

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

static void
dump_std_video_h265_sps_flags (const StdVideoH265SpsFlags * flags)
{
  start_object ("StdVideoH265SpsFlags");
  print_integer ("sps_temporal_id_nesting_flag",
      flags->sps_temporal_id_nesting_flag);
  print_integer ("separate_colour_plane_flag",
      flags->separate_colour_plane_flag);
  print_integer ("scaling_list_enabled_flag", flags->scaling_list_enabled_flag);
  print_integer ("sps_scaling_list_data_present_flag",
      flags->sps_scaling_list_data_present_flag);
  print_integer ("amp_enabled_flag", flags->amp_enabled_flag);
  print_integer ("sample_adaptive_offset_enabled_flag",
      flags->sample_adaptive_offset_enabled_flag);
  print_integer ("pcm_enabled_flag", flags->pcm_enabled_flag);
  print_integer ("pcm_loop_filter_disabled_flag",
      flags->pcm_loop_filter_disabled_flag);
  print_integer ("long_term_ref_pics_present_flag",
      flags->long_term_ref_pics_present_flag);
  print_integer ("sps_temporal_mvp_enabled_flag",
      flags->sps_temporal_mvp_enabled_flag);
  print_integer ("strong_intra_smoothing_enabled_flag",
      flags->strong_intra_smoothing_enabled_flag);
  print_integer ("vui_parameters_present_flag",
      flags->vui_parameters_present_flag);
  print_integer ("sps_extension_present_flag",
      flags->sps_extension_present_flag);
  print_integer ("sps_range_extension_flag", flags->sps_range_extension_flag);
  print_integer ("transform_skip_rotation_enabled_flag",
      flags->transform_skip_rotation_enabled_flag);
  print_integer ("transform_skip_context_enabled_flag",
      flags->transform_skip_context_enabled_flag);
  print_integer ("implicit_rdpcm_enabled_flag",
      flags->implicit_rdpcm_enabled_flag);
  print_integer ("explicit_rdpcm_enabled_flag",
      flags->explicit_rdpcm_enabled_flag);
  print_integer ("extended_precision_processing_flag",
      flags->extended_precision_processing_flag);
  print_integer ("intra_smoothing_disabled_flag",
      flags->intra_smoothing_disabled_flag);
  print_integer ("high_precision_offsets_enabled_flag",
      flags->high_precision_offsets_enabled_flag);
  print_integer ("persistent_rice_adaptation_enabled_flag",
      flags->persistent_rice_adaptation_enabled_flag);
  print_integer ("cabac_bypass_alignment_enabled_flag",
      flags->cabac_bypass_alignment_enabled_flag);
  print_integer ("sps_scc_extension_flag", flags->sps_scc_extension_flag);
  print_integer ("sps_curr_pic_ref_enabled_flag",
      flags->sps_curr_pic_ref_enabled_flag);
  print_integer ("palette_mode_enabled_flag", flags->palette_mode_enabled_flag);
  print_integer ("sps_palette_predictor_initializer_present_flag",
      flags->sps_palette_predictor_initializer_present_flag);
  print_integer ("intra_boundary_filtering_disabled_flag",
      flags->intra_boundary_filtering_disabled_flag);
  end_object ();
}

static void
dump_std_video_h265_scaling_lists (const StdVideoH265ScalingLists * lists)
{
  start_object ("StdVideoH265ScalingLists");
  start_array ("ScalingList4x4");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList4x4[i][j]);
  end_array ();
  start_array ("ScalingList8x8");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList8x8[i][j]);
  end_array ();
  start_array ("ScalingList16x16");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList16x16[i][j]);
  end_array ();
  start_array ("ScalingList32x32");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++)
    for (int j = 0; j < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_ELEMENTS; j++)
      print_integer (NULL, lists->ScalingList32x32[i][j]);
  end_array ();
  start_array ("ScalingistDCCoef16x16");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++)
    print_integer (NULL, lists->ScalingListDCCoef16x16[i]);
  end_array ();
  start_array ("ScalingistDCCoef32x32");
  for (int i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++)
    print_integer (NULL, lists->ScalingListDCCoef32x32[i]);
  end_array ();
  end_object ();
}

static void
dump_std_video_h265_dec_pic_buf_mgr (const StdVideoH265DecPicBufMgr * bmgr)
{
  start_object ("StdVideoH265DecPicBufMgr");
  start_array ("max_latency_increase_plus1");
  for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE; i++)
    print_integer (NULL, bmgr->max_latency_increase_plus1[i]);
  end_array ();
  start_array ("max_dec_pic_buffering_minus1");
  for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE; i++)
    print_integer (NULL, bmgr->max_dec_pic_buffering_minus1[i]);
  end_array ();
  start_array ("max_num_reorder_pics");
  for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE; i++)
    print_integer (NULL, bmgr->max_num_reorder_pics[i]);
  end_array ();
  end_object ();
}

static void
dump_std_video_h265_sps_vui_flags (const StdVideoH265SpsVuiFlags * flags)
{
  start_object ("StdVideoH265SpsVuiFlags");
  print_integer ("aspect_ratio_info_present_flag",
      flags->aspect_ratio_info_present_flag);
  print_integer ("overscan_info_present_flag",
      flags->overscan_info_present_flag);
  print_integer ("overscan_appropriate_flag",
      flags->overscan_appropriate_flag);
  print_integer ("video_signal_type_present_flag",
      flags->video_signal_type_present_flag);
  print_integer ("video_full_range_flag", flags->video_full_range_flag);
  print_integer ("colour_description_present_flag",
      flags->colour_description_present_flag);
  print_integer ("chroma_loc_info_present_flag",
      flags->chroma_loc_info_present_flag);
  print_integer ("neutral_chroma_indication_flag",
      flags->neutral_chroma_indication_flag);
  print_integer ("field_seq_flag", flags->field_seq_flag);
  print_integer ("frame_field_info_present_flag",
      flags->frame_field_info_present_flag);
  print_integer ("default_display_window_flag",
      flags->default_display_window_flag);
  print_integer ("vui_timing_info_present_flag",
      flags->vui_timing_info_present_flag);
  print_integer ("vui_poc_proportional_to_timing_flag",
      flags->vui_poc_proportional_to_timing_flag);
  print_integer ("vui_hrd_parameters_present_flag",
      flags->vui_hrd_parameters_present_flag);
  print_integer ("bitstream_restriction_flag",
      flags->bitstream_restriction_flag);
  print_integer ("tiles_fixed_structure_flag",
      flags->tiles_fixed_structure_flag);
  print_integer ("motion_vectors_over_pic_boundaries_flag",
      flags->motion_vectors_over_pic_boundaries_flag);
  print_integer ("restricted_ref_pic_lists_flag",
      flags->restricted_ref_pic_lists_flag);
  end_object ();
}

static void
dump_std_video_h265_hrd_flags (const StdVideoH265HrdFlags * flags)
{
  start_object ("StdVideoH265HrdFlags");
  print_integer ("nal_hrd_parameters_present_flag",
      flags->nal_hrd_parameters_present_flag);
  print_integer ("vcl_hrd_parameters_present_flag",
      flags->vcl_hrd_parameters_present_flag);
  print_integer ("sub_pic_hrd_params_present_flag",
      flags->sub_pic_hrd_params_present_flag);
  print_integer ("sub_pic_cpb_params_in_pic_timing_sei_flag",
      flags->sub_pic_cpb_params_in_pic_timing_sei_flag);
  print_integer ("fixed_pic_rate_general_flag",
      flags->fixed_pic_rate_general_flag);
  print_integer ("fixed_pic_rate_within_cvs_flag",
      flags->fixed_pic_rate_within_cvs_flag);
  print_integer ("low_delay_hrd_flag",
      flags->low_delay_hrd_flag);
  end_object ();
}

static void
dump_std_video_h265_hrd (const StdVideoH265HrdParameters * hrd)
{
  start_object ("StdVideoH265HrdParameters");
  dump_std_video_h265_hrd_flags (&hrd->flags);
  print_integer ("tick_divisor_minus2", hrd->tick_divisor_minus2);
  print_integer ("du_cpb_removal_delay_increment_length_minus1",
      hrd->du_cpb_removal_delay_increment_length_minus1);
  print_integer ("dpb_output_delay_du_length_minus1",
      hrd->dpb_output_delay_du_length_minus1);
  print_integer ("bit_rate_scale", hrd->bit_rate_scale);
  print_integer ("cpb_size_scale", hrd->cpb_size_scale);
  print_integer ("cpb_size_du_scale", hrd->cpb_size_du_scale);
  print_integer ("initial_cpb_removal_delay_length_minus1",
      hrd->initial_cpb_removal_delay_length_minus1);
  print_integer ("au_cpb_removal_delay_length_minus1",
      hrd->au_cpb_removal_delay_length_minus1);
  print_integer ("dpb_output_delay_length_minus1",
      hrd->dpb_output_delay_du_length_minus1);
  start_array ("cpb_cnt_minus1");
  for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE; i++)
    print_integer (NULL, hrd->cpb_cnt_minus1[i]);
  end_array ();
  start_array ("elemental_duration_in_tc_minus1");
  for (int i = 0; i < STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE; i++)
    print_integer (NULL, hrd->elemental_duration_in_tc_minus1[i]);
  end_array ();
  // TODO:
  //const StdVideoH265SubLayerHrdParameters*    pSubLayerHrdParametersNal[STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE];
  //const StdVideoH265SubLayerHrdParameters*    pSubLayerHrdParametersVcl[STD_VIDEO_H265_SUBLAYERS_MINUS1_LIST_SIZE];
  end_object ();
}

static void
dump_std_video_h265_sps_vui (const StdVideoH265SequenceParameterSetVui * vui)
{
  start_object ("StdVideoH265SequenceParameterSetVui");
  dump_std_video_h265_sps_vui_flags (&vui->flags);
  print_integer ("aspect_ratio_idc", vui->aspect_ratio_idc);
  print_integer ("sar_width", vui->sar_width);
  print_integer ("sar_height", vui->sar_height);
  print_integer ("video_format", vui->video_format);
  print_integer ("colour_primaries", vui->colour_primaries);
  print_integer ("transfer_characteristics", vui->transfer_characteristics);
  print_integer ("matrix_coeffs", vui->matrix_coeffs);
  print_integer ("chroma_sample_loc_type_top_field",
      vui->chroma_sample_loc_type_top_field);
  print_integer ("chroma_sample_loc_type_bottom_field",
      vui->chroma_sample_loc_type_bottom_field);
  print_integer ("def_disp_win_left_offset", vui->def_disp_win_left_offset);
  print_integer ("def_disp_win_right_offset", vui->def_disp_win_right_offset);
  print_integer ("def_disp_win_top_offset", vui->def_disp_win_top_offset);
  print_integer ("def_disp_win_bottom_offset", vui->def_disp_win_bottom_offset);
  print_integer ("vui_num_units_in_tick", vui->vui_num_units_in_tick);
  print_integer ("vui_time_scale", vui->vui_time_scale);
  print_integer ("vui_num_ticks_poc_diff_one_minus1",
      vui->vui_num_ticks_poc_diff_one_minus1);
  if (vui->pHrdParameters)
    dump_std_video_h265_hrd (vui->pHrdParameters);
  print_integer ("min_spatial_segmentation_idc",
      vui->min_spatial_segmentation_idc);
  print_integer ("max_bytes_per_pic_denom", vui->max_bytes_per_pic_denom);
  print_integer ("max_bits_per_min_cu_denom", vui->max_bits_per_min_cu_denom);
  print_integer ("log2_max_mv_length_horizontal",
      vui->log2_max_mv_length_horizontal);
  print_integer ("log2_max_mv_length_vertical2",
      vui->log2_max_mv_length_vertical);
  end_object ();
}

static void
dump_std_video_h265_predictor_palette_entries
    (const StdVideoH265PredictorPaletteEntries * entry)
{
  start_object ("StdVideoH265PredictorPaletteEntries");
  start_array ("PredictorPaletteEntries");
  for (int i = 0; i < STD_VIDEO_H265_PREDICTOR_PALETTE_COMPONENTS_LIST_SIZE; i++)
    for (int j = 0; j < STD_VIDEO_H265_PREDICTOR_PALETTE_COMP_ENTRIES_LIST_SIZE; j++)
      print_integer (NULL, entry->PredictorPaletteEntries[i][j]);
  end_array ();
  end_object ();
}

static void
dump_std_video_h265_sps (const StdVideoH265SequenceParameterSet * sps)
{
  start_object ("StdVideoH265SequenceParameterSet");
  dump_std_video_h265_sps_flags (&sps->flags);
  print_integer ("profile_idc", sps->profile_idc);
  print_integer ("level_idc", sps->level_idc);
  print_integer ("pic_width_in_luma_samples", sps->pic_width_in_luma_samples);
  print_integer ("pic_height_in_luma_samples", sps->pic_height_in_luma_samples);
  print_integer ("sps_video_parameter_set_id", sps->sps_video_parameter_set_id);
  print_integer ("sps_max_sub_layers_minus1", sps->sps_max_sub_layers_minus1);
  print_integer ("sps_seq_parameter_set_id", sps->sps_seq_parameter_set_id);
  print_integer ("chroma_format_idc", sps->chroma_format_idc);
  print_integer ("bit_depth_luma_minus8", sps->bit_depth_chroma_minus8);
  print_integer ("bit_depth_chroma_minus8", sps->bit_depth_chroma_minus8);
  print_integer ("log2_max_pic_order_cnt_lsb_minus4",
      sps->log2_max_pic_order_cnt_lsb_minus4);
  print_integer ("log2_min_luma_coding_block_size_minus3",
      sps->log2_min_luma_coding_block_size_minus3);
  print_integer ("log2_diff_max_min_luma_coding_block_size",
      sps->log2_diff_max_min_luma_transform_block_size);
  print_integer ("log2_min_luma_transform_block_size_minus2",
      sps->log2_min_luma_transform_block_size_minus2);
  print_integer ("log2_diff_max_min_luma_transform_block_size",
      sps->log2_diff_max_min_luma_coding_block_size);
  print_integer ("max_transform_hierarchy_depth_inter",
      sps->max_transform_hierarchy_depth_inter);
  print_integer ("max_transform_hierarchy_depth_intra",
      sps->max_transform_hierarchy_depth_intra);
  print_integer ("num_short_term_ref_pic_sets",
      sps->num_short_term_ref_pic_sets);
  print_integer ("num_long_term_ref_pics_sps", sps->num_long_term_ref_pics_sps);
  print_integer ("pcm_sample_bit_depth_luma_minus1",
      sps->pcm_sample_bit_depth_luma_minus1);
  print_integer ("pcm_sample_bit_depth_chroma_minus1",
      sps->pcm_sample_bit_depth_chroma_minus1);
  print_integer ("log2_min_pcm_luma_coding_block_size_minus3",
      sps->log2_min_pcm_luma_coding_block_size_minus3);
  print_integer ("log2_diff_max_min_pcm_luma_coding_block_size",
      sps->log2_diff_max_min_pcm_luma_coding_block_size);
  print_integer ("conf_win_left_offset", sps->conf_win_left_offset);
  print_integer ("conf_win_right_offset", sps->conf_win_right_offset);
  print_integer ("conf_win_top_offset", sps->conf_win_top_offset);
  print_integer ("conf_win_bottom_offset", sps->conf_win_bottom_offset);
  if (sps->pDecPicBufMgr)
    dump_std_video_h265_dec_pic_buf_mgr (sps->pDecPicBufMgr);
  if (sps->pScalingLists)
    dump_std_video_h265_scaling_lists (sps->pScalingLists);
  if (sps->pSequenceParameterSetVui)
    dump_std_video_h265_sps_vui (sps->pSequenceParameterSetVui);
  print_integer ("palette_max_size", sps->palette_max_size);
  print_integer ("delta_palette_max_predictor_size",
      sps->delta_palette_max_predictor_size);
  print_integer ("motion_vector_resolution_control_idc",
      sps->motion_vector_resolution_control_idc);
  print_integer ("sps_num_palette_predictor_initializer_minus1",
      sps->sps_num_palette_predictor_initializer_minus1);
  if (sps->pPredictorPaletteEntries)
    dump_std_video_h265_predictor_palette_entries (sps->pPredictorPaletteEntries);
  end_object ();
}

static void
dump_std_video_h265_pps_flags (const StdVideoH265PpsFlags * flags)
{
  start_object ("StdVideoH265PpsFlags");
  print_integer ("dependent_slice_segments_enabled_flag",
      flags->dependent_slice_segments_enabled_flag);
  print_integer ("output_flag_present_flag", flags->output_flag_present_flag);
  print_integer ("sign_data_hiding_enabled_flag",
      flags->sign_data_hiding_enabled_flag);
  print_integer ("cabac_init_present_flag", flags->cabac_init_present_flag);
  print_integer ("constrained_intra_pred_flag",
      flags->constrained_intra_pred_flag);
  print_integer ("transform_skip_enabled_flag",
      flags->transform_skip_enabled_flag);
  print_integer ("cu_qp_delta_enabled_flag", flags->cu_qp_delta_enabled_flag);
  print_integer ("pps_slice_chroma_qp_offsets_present_flag",
      flags->pps_slice_chroma_qp_offsets_present_flag);
  print_integer ("weighted_pred_flag", flags->weighted_pred_flag);
  print_integer ("weighted_bipred_flag", flags->weighted_bipred_flag);
  print_integer ("transquant_bypass_enabled_flag",
      flags->transquant_bypass_enabled_flag);
  print_integer ("tiles_enabled_flag", flags->tiles_enabled_flag);
  print_integer ("entropy_coding_sync_enabled_flag",
      flags->entropy_coding_sync_enabled_flag);
  print_integer ("uniform_spacing_flag", flags->uniform_spacing_flag);
  print_integer ("loop_filter_across_tiles_enabled_flag",
      flags->loop_filter_across_tiles_enabled_flag);
  print_integer ("pps_loop_filter_across_slices_enabled_flag",
      flags->pps_loop_filter_across_slices_enabled_flag);
  print_integer ("deblocking_filter_control_present_flag",
      flags->deblocking_filter_control_present_flag);
  print_integer ("deblocking_filter_override_enabled_flag",
      flags->deblocking_filter_override_enabled_flag);
  print_integer ("pps_deblocking_filter_disabled_flag",
      flags->pps_deblocking_filter_disabled_flag);
  print_integer ("pps_scaling_list_data_present_flag",
      flags->pps_scaling_list_data_present_flag);
  print_integer ("lists_modification_present_flag",
      flags->lists_modification_present_flag);
  print_integer ("slice_segment_header_extension_present_flag",
      flags->slice_segment_header_extension_present_flag);
  print_integer ("pps_extension_present_flag",
      flags->pps_extension_present_flag);
  print_integer ("cross_component_prediction_enabled_flag",
      flags->cross_component_prediction_enabled_flag);
  print_integer ("chroma_qp_offset_list_enabled_flag",
      flags->chroma_qp_offset_list_enabled_flag);
  print_integer ("pps_curr_pic_ref_enabled_flag",
      flags->pps_curr_pic_ref_enabled_flag);
  print_integer ("residual_adaptive_colour_transform_enabled_flag",
      flags->residual_adaptive_colour_transform_enabled_flag);
  print_integer ("pps_slice_act_qp_offsets_present_flag",
      flags->pps_slice_act_qp_offsets_present_flag);
  print_integer ("pps_palette_predictor_initializer_present_flag",
      flags->pps_palette_predictor_initializer_present_flag);
  print_integer ("monochrome_palette_flag", flags->monochrome_palette_flag);
  print_integer ("pps_range_extension_flag", flags->pps_range_extension_flag);
  end_object ();
}

static void
dump_std_video_h265_pps (const StdVideoH265PictureParameterSet * pps)
{
  start_object ("StdVideoH265PictureParameterSet");
  dump_std_video_h265_pps_flags (&pps->flags);
  print_integer ("pps_pic_parameter_set_id", pps->pps_pic_parameter_set_id);
  print_integer ("pps_seq_parameter_set_id", pps->pps_seq_parameter_set_id);
  print_integer ("num_extra_slice_header_bits",
      pps->num_extra_slice_header_bits);
  print_integer ("num_ref_idx_l0_default_active_minus1",
      pps->num_ref_idx_l0_default_active_minus1);
  print_integer ("num_ref_idx_l1_default_active_minus1",
      pps->num_ref_idx_l1_default_active_minus1);
  print_integer ("init_qp_minus26", pps->init_qp_minus26);
  print_integer ("diff_cu_qp_delta_depth", pps->diff_cu_qp_delta_depth);
  print_integer ("pps_cb_qp_offset", pps->pps_cb_qp_offset);
  print_integer ("pps_cr_qp_offset", pps->pps_cr_qp_offset);
  print_integer ("num_tile_columns_minus1", pps->num_tile_columns_minus1);
  print_integer ("num_tile_rows_minus1", pps->num_tile_rows_minus1);
  start_array ("column_width_minus1");
  for (int i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE; i++)
    print_integer (NULL, pps->column_width_minus1[i]);
  end_array ();
  start_array ("row_height_minus1");
  for (int i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE; i++)
    print_integer (NULL, pps->row_height_minus1[i]);
  end_array ();
  print_integer ("pps_beta_offset_div2", pps->pps_beta_offset_div2);
  print_integer ("pps_tc_offset_div2", pps->pps_tc_offset_div2);
  print_integer ("log2_parallel_merge_level_minus2",
      pps->log2_parallel_merge_level_minus2);
  if (pps->pScalingLists)
    dump_std_video_h265_scaling_lists (pps->pScalingLists);
  print_integer ("log2_max_transform_skip_block_size_minus2",
      pps->log2_max_transform_skip_block_size_minus2);
  print_integer ("diff_cu_chroma_qp_offset_depth",
      pps->diff_cu_chroma_qp_offset_depth);
  print_integer ("chroma_qp_offset_list_len_minus1",
      pps->chroma_qp_offset_list_len_minus1);
  start_array ("cb_qp_offset_list");
  for (int i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE; i++)
    print_integer (NULL, pps->cb_qp_offset_list[i]);
  end_array ();
  start_array ("cr_qp_offset_list");
  for (int i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE; i++)
    print_integer (NULL, pps->cr_qp_offset_list[i]);
  end_array ();
  print_integer ("log2_sao_offset_scale_luma", pps->log2_sao_offset_scale_luma);
  print_integer ("log2_sao_offset_scale_chroma",
      pps->log2_sao_offset_scale_chroma);
  print_integer ("pps_act_y_qp_offset_plus5", pps->pps_act_y_qp_offset_plus5);
  print_integer ("pps_act_cb_qp_offset_plus5", pps->pps_act_cb_qp_offset_plus5);
  print_integer ("pps_act_cr_qp_offset_plus5", pps->pps_act_cr_qp_offset_plus5);
  print_integer ("pps_num_palette_predictor_initializer",
      pps->pps_num_palette_predictor_initializer);
  print_integer ("luma_bit_depth_entry_minus8",
      pps->luma_bit_depth_entry_minus8);
  print_integer ("chroma_bit_depth_entry_minus8",
      pps->chroma_bit_depth_entry_minus8);
  if (pps->pPredictorPaletteEntries)
    dump_std_video_h265_predictor_palette_entries (pps->pPredictorPaletteEntries);
  end_object ();
}

static void
dump_std_video_h265_vps_flags (const StdVideoH265VpsFlags * flags)
{
  start_object ("StdVideoH265VpsFlags");
  print_integer ("vps_temporal_id_nesting_flag",
      flags->vps_temporal_id_nesting_flag);
  print_integer ("vps_sub_layer_ordering_info_present_flag",
      flags->vps_sub_layer_ordering_info_present_flag);
  print_integer ("vps_timing_info_present_flag",
      flags->vps_timing_info_present_flag);
  print_integer ("vps_poc_proportional_to_timing_flag",
      flags->vps_poc_proportional_to_timing_flag);
  end_object ();
}

static void
dump_std_video_h265_vps (const StdVideoH265VideoParameterSet * vps)
{
  start_object ("StdVideoH265VideoParameterSet");
  dump_std_video_h265_vps_flags (&vps->flags);
  print_integer ("vps_video_parameter_set_id", vps->vps_video_parameter_set_id);
  print_integer ("vps_max_sub_layers_minus1", vps->vps_max_sub_layers_minus1);
  print_integer ("vps_num_units_in_tick", vps->vps_num_units_in_tick);
  print_integer ("vps_time_scale", vps->vps_time_scale);
  print_integer ("vps_num_ticks_poc_diff_one_minus1",
      vps->vps_num_ticks_poc_diff_one_minus1);
  if (vps->pDecPicBufMgr)
    dump_std_video_h265_dec_pic_buf_mgr (vps->pDecPicBufMgr);
  if (vps->pHrdParameters)
    dump_std_video_h265_hrd (vps->pHrdParameters);
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
      dump_std_video_h265_vps (params->pH265Vps);
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H265_SPS:
      dump_std_video_h265_sps (params->pH265Sps);
      break;
    case VK_PICTURE_PARAMETERS_UPDATE_H265_PPS:
      dump_std_video_h265_pps (params->pH265Pps);
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

static void
dump_parser_h265_picture_data (const VkParserHevcPictureData * data)
{
  start_object ("VkParserHevcPictureData");
  dump_std_video_h265_sps (data->pStdSps);
  // VkParserVideoRefCountBase*              pSpsClientObject;
  dump_std_video_h265_pps (data->pStdPps);
  // VkParserVideoRefCountBase*              pPpsClientObject;

  print_integer ("pic_parameter_set_id", data->pic_parameter_set_id);       // PPS ID
  print_integer ("seq_parameter_set_id", data->seq_parameter_set_id);       // SPS ID
  print_integer ("vps_video_parameter_set_id",
      data->vps_video_parameter_set_id); // VPS ID

  print_integer ("IrapPicFlag", data->IrapPicFlag);
  print_integer ("IdrPicFlag", data->IdrPicFlag);

  // RefPicSets
  print_integer ("NumBitsForShortTermRPSInSlice",
      data->NumBitsForShortTermRPSInSlice);
  print_integer ("NumDeltaPocsOfRefRpsIdx", data->NumDeltaPocsOfRefRpsIdx);
  print_integer ("NumPocTotalCurr", data->NumPocTotalCurr);
  print_integer ("NumPocStCurrBefore", data->NumPocStCurrBefore);
  print_integer ("NumPocStCurrAfter", data->NumPocStCurrAfter);
  print_integer ("NumPocLtCurr", data->NumPocLtCurr);
  print_integer ("CurrPicOrderCntVal", data->CurrPicOrderCntVal);
  // VkPicIf* RefPics[16];
  start_array ("PicOrderCntVal");
  for (int i = 0; i < 16; i++)
    print_integer (NULL, data->PicOrderCntVal[i]);
  end_array ();
  start_array ("IsLongTerm");
  for (int i = 0; i < 16; i++)
    print_integer (NULL, data->IsLongTerm[i]); // 1=long-term reference
  end_array ();
  start_array ("RefPicSetStCurrBefore");
  for (int i = 0; i < 8; i++)
    print_integer (NULL, data->RefPicSetStCurrBefore[i]);
  end_array ();
  start_array ("RefPicSetStCurrAfter");
  for (int i = 0; i < 8; i++)
    print_integer (NULL, data->RefPicSetStCurrAfter[i]);
  end_array ();
  start_array ("RefPicSetLtCurr");
  for (int i = 0; i < 8; i++)
    print_integer (NULL, data->RefPicSetLtCurr[i]);
  end_array ();

  // various profile related
  // 0 = invalid, 1 = Main, 2 = Main10, 3 = still picture, 4 = Main 12,
  // 5 = MV-HEVC Main8
  print_integer ("ProfileLevel", data->ProfileLevel);
  print_integer ("ColorPrimaries", data->ColorPrimaries); // ColorPrimariesBTXXXX enum
  print_integer ("bit_depth_luma_minus8", data->bit_depth_luma_minus8);
  print_integer ("bit_depth_chroma_minus8", data->bit_depth_chroma_minus8);

  // MV-HEVC related fields
  print_integer ("mv_hevc_enable", data->mv_hevc_enable);
  print_integer ("nuh_layer_id", data->nuh_layer_id);
  print_integer ("default_ref_layers_active_flag",
      data->default_ref_layers_active_flag);
  print_integer ("NumDirectRefLayers", data->NumDirectRefLayers);
  print_integer ("max_one_active_ref_layer_flag",
      data->max_one_active_ref_layer_flag);
  print_integer ("poc_lsb_not_present_flag", data->poc_lsb_not_present_flag);
  start_array ("pad0");
  for (int i = 0; i < 2; i++)
    print_integer (NULL, data->pad0[i]);
  end_array ();

  print_integer ("NumActiveRefLayerPics0", data->NumActiveRefLayerPics0);
  print_integer ("NumActiveRefLayerPics1", data->NumActiveRefLayerPics1);
  start_array ("RefPicSetInterLayer0");
  for (int i = 0; i < 8; i++)
    print_integer (NULL, data->RefPicSetInterLayer0[i]);
  end_array ();
  start_array ("RefPicSetInterLayer1");
  for (int i = 0; i < 8; i++)
    print_integer (NULL, data->RefPicSetInterLayer1[i]);
  end_array ();
  end_object ();
}

void
dump_parser_picture_data (VkVideoCodecOperationFlagBitsKHR codec, VkParserPictureData * pic)
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

  if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT)
       dump_parser_h264_picture_data (&pic->CodecSpecific.h264);
  else if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT)
       dump_parser_h265_picture_data (&pic->CodecSpecific.hevc);

  end_object ();
}
