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

#include "h264dec.h"

#include "videoparser.h"
#include "videoutils.h"

#define GST_H264_DEC(obj)           ((GstH264Dec *) obj)
#define GST_H264_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE(obj), GstH264DecClass))
#define GST_H264_DEC_CLASS(klass)   ((GstH264DecClass *) klass)

GST_DEBUG_CATEGORY_EXTERN(gst_video_parser_debug);
#define GST_CAT_DEFAULT gst_video_parser_debug

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/x-raw, format=(string)NV12"));

typedef struct _GstH264Dec GstH264Dec;
struct _GstH264Dec
{
  GstH264Decoder parent;
  VkParserVideoDecodeClient *client;

  gint max_dpb_size;
};

struct VkPic
{
  VkPicIf *pic;
  VkParserPictureData data;
  gint n_slices;
};

enum {
  PROP_USER_DATA = 1,
};

G_DEFINE_FINAL_TYPE(GstH264Dec, gst_h264_dec, GST_TYPE_H264_DECODER)

static gpointer parent_class = NULL;

static VkPic *
vk_pic_new (VkPicIf *pic)
{
  VkPic *vkpic = g_new0(struct VkPic, 1);

  vkpic->pic = pic;
  return vkpic;
}

static void
vk_pic_free (gpointer data)
{
  VkPic *vkpic = static_cast<VkPic *>(data);

  vkpic->pic->Release();
  g_free (vkpic);
}

static bool
profile_is_svc(GstCaps * caps)
{
  const GstStructure *structure = gst_caps_get_structure(caps, 0);
  const gchar *profile = gst_structure_get_string(structure, "profile");

  return g_str_has_prefix(profile, "scalable");
}

static GstFlowReturn
gst_h264_dec_new_sequence(GstH264Decoder * decoder, const GstH264SPS * sps, gint max_dpb_size)
{
  GstH264Dec *self = GST_H264_DEC(decoder);
  GstVideoDecoder *dec = GST_VIDEO_DECODER(decoder);
  GstVideoCodecState *state;
  VkParserSequenceInfo seqInfo = { };
  guint dar_n = 0, dar_d = 0;

  seqInfo.eCodec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;
  seqInfo.isSVC = profile_is_svc(decoder->input_state->caps);
  seqInfo.frameRate = pack_framerate(GST_VIDEO_INFO_FPS_N(&decoder->input_state->info), GST_VIDEO_INFO_FPS_D(&decoder->input_state->info));
  seqInfo.bProgSeq = sps->frame_mbs_only_flag;
  if (sps->frame_cropping_flag) {
    seqInfo.nDisplayWidth = sps->crop_rect_width;
    seqInfo.nDisplayHeight = sps->crop_rect_height;
  } else {
    seqInfo.nDisplayWidth = sps->width;
    seqInfo.nDisplayHeight = sps->height;
  }
  seqInfo.nCodedWidth = sps->width;
  seqInfo.nCodedHeight = sps->height;
  seqInfo.nMaxWidth = 0;
  seqInfo.nMaxHeight = 0;
  seqInfo.nChromaFormat = sps->chroma_format_idc;         // Chroma Format (0=4:0:0, 1=4:2:0, 2=4:2:2, 3=4:4:4)
  seqInfo.uBitDepthLumaMinus8 = sps->bit_depth_luma_minus8; // Luma bit depth (0=8bit)
  seqInfo.uBitDepthChromaMinus8 = sps->bit_depth_chroma_minus8; // Chroma bit depth (0=8bit)
  if (sps->vui_parameters_present_flag) {
    seqInfo.uVideoFullRange = sps->vui_parameters.video_full_range_flag;       // 0=16-235, 1=0-255
    if (sps->vui_parameters.nal_hrd_parameters_present_flag)
         seqInfo.lBitrate = sps->vui_parameters.nal_hrd_parameters.bit_rate_scale;
    else if (sps->vui_parameters.vcl_hrd_parameters_present_flag)
         seqInfo.lBitrate = sps->vui_parameters.vcl_hrd_parameters.bit_rate_scale;
    seqInfo.lVideoFormat = sps->vui_parameters.video_format; // Video Format (VideoFormatXXX)
    seqInfo.lColorPrimaries = sps->vui_parameters.colour_primaries; // Colour Primaries (ColorPrimariesXXX)
    seqInfo.lTransferCharacteristics = sps->vui_parameters.transfer_characteristics;
    seqInfo.lMatrixCoefficients = sps->vui_parameters.matrix_coefficients;
  }

  if (gst_video_calculate_display_ratio (&dar_n, &dar_d,
          seqInfo.nDisplayWidth, seqInfo.nDisplayHeight,
          GST_VIDEO_INFO_PAR_N(&decoder->input_state->info),
          GST_VIDEO_INFO_PAR_D(&decoder->input_state->info), 1, 1)) {
    seqInfo.lDARWidth = dar_n;
    seqInfo.lDARHeight = dar_d;
  }

  seqInfo.cbSequenceHeader = 0; // Number of bytes in SequenceHeaderData
  seqInfo.nMinNumDecodeSurfaces = max_dpb_size + 1;
  // seqInfo.SequenceHeaderData[1024];
  seqInfo.pbSideData = nullptr;
  seqInfo.cbSideData = 0;

  if (self->client)
    self->max_dpb_size = self->client->BeginSequence (&seqInfo);

  state = gst_video_decoder_set_output_state (dec, GST_VIDEO_FORMAT_NV12, seqInfo.nDisplayWidth, seqInfo.nDisplayHeight, decoder->input_state);
  gst_video_codec_state_unref (state);

  gst_video_decoder_negotiate (dec);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_decode_slice(GstH264Decoder * decoder, GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  VkPic *vkpic = static_cast<VkPic *>(gst_h264_picture_get_user_data(picture));

  vkpic->n_slices++;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_picture(GstH264Decoder * decoder, GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstH264Dec *self = GST_H264_DEC(decoder);
  VkPicIf *pic = nullptr;
  VkPic *vkpic;

  if (self->client) {
    if (!self->client->AllocPictureBuffer(&pic))
      return GST_FLOW_ERROR;
  }

  vkpic = vk_pic_new(pic);
  gst_h264_picture_set_user_data(picture, vkpic, vk_pic_free);

  frame->output_buffer = gst_buffer_new();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_field_picture(GstH264Decoder * decoder, GstH264Picture * first_field, GstH264Picture * second_field)
{
  GstH264Dec *self = GST_H264_DEC(decoder);
  VkPicIf *pic = nullptr;
  VkPic *vkpic;

  if (self->client) {
    if (!self->client->AllocPictureBuffer(&pic))
      return GST_FLOW_ERROR;
  }

  vkpic = vk_pic_new(pic);
  gst_h264_picture_set_user_data(second_field, vkpic, vk_pic_free);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_output_picture(GstH264Decoder * decoder, GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  GstH264Dec *self = GST_H264_DEC(decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h264_picture_get_user_data(picture));;

  if (self->client) {
    if (!self->client->DisplayPicture(vkpic->pic, picture->system_frame_number)) {
      gst_h264_picture_unref (picture);
      return GST_FLOW_ERROR;
    }
  }

  gst_h264_picture_unref (picture);
  return gst_video_decoder_finish_frame(GST_VIDEO_DECODER (decoder), frame);
}

static GstFlowReturn
gst_h264_dec_end_picture(GstH264Decoder * decoder, GstH264Picture * picture)
{
  GstH264Dec *self = GST_H264_DEC(decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h264_picture_get_user_data(picture));;

  vkpic->data.nNumSlices = vkpic->n_slices; // Number of slices(tiles in case of AV1) in this picture
  vkpic->n_slices = 0;

  if (self->client) {
    if (!self->client->DecodePicture(&vkpic->data))
      return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_start_picture(GstH264Decoder * decoder, GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  //GstH264Dec *self = GST_H264_DEC(decoder);
  VkPic *vkpic = reinterpret_cast<VkPic *>(gst_h264_picture_get_user_data(picture));
  GstH264PPS *pps = slice->header.pps;
  GstH264SPS *sps = pps->sequence;

  vkpic->data.PicWidthInMbs = sps->width / 16; // Coded Frame Size
  vkpic->data.FrameHeightInMbs = sps->height / 16; // Coded Frame Height
  vkpic->data.pCurrPic = vkpic->pic;
  vkpic->data.field_pic_flag = slice->header.field_pic_flag; // 0=frame picture, 1=field picture
  vkpic->data.bottom_field_flag = slice->header.bottom_field_flag; // 0=top field, 1=bottom field (ignored if field_pic_flag=0)
  vkpic->data.second_field = picture->second_field; // Second field of a complementary field pair
  vkpic->data.progressive_frame = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) == 0; // Frame is progressive
  vkpic->data.top_field_first = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0;
  vkpic->data.repeat_first_field = 2; // For 3:2 pulldown (number of additional fields,
        // 2=frame doubling, 4=frame tripling)
  vkpic->data.ref_pic_flag = picture->ref_pic; // Frame is a reference frame
  //vkpic->data.intra_pic_flag; // Frame is entirely intra coded (no temporal
        // dependencies)
  vkpic->data.chroma_format = sps->chroma_format_idc; // Chroma Format (should match sequence info)
  vkpic->data.picture_order_count = picture->pic_order_cnt; // picture order count (if known)

  vkpic->data.pbSideData = nullptr; // Encryption Info
  vkpic->data.nSideDataLen = 0; // Encryption Info length

  // Bitstream data
  //vkpic->data.nBitstreamDataLen; // Number of bytes in bitstream data buffer
  //vkpic->data.pBitstreamData; // Ptr to bitstream data for this picture (slice-layer)
  //vkpic->data.pSliceDataOffsets; // nNumSlices entries, contains offset of each slice
        // within the bitstream data buffer

  const StdVideoH264SequenceParameterSet std_sps = {
    .profile_idc = static_cast<StdVideoH264ProfileIdc>(sps->profile_idc),
    .level_idc = static_cast<StdVideoH264Level>(sps->level_idc),
    .seq_parameter_set_id = static_cast<uint8_t>(sps->id),
    .chroma_format_idc = static_cast<StdVideoH264ChromaFormatIdc>(sps->chroma_format_idc),
    .bit_depth_luma_minus8 = sps->bit_depth_luma_minus8,
    .bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8,
    .log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4,
    .pic_order_cnt_type = static_cast<StdVideoH264PocType>(sps->pic_order_cnt_type),
    .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4,
    .offset_for_non_ref_pic = sps->offset_for_non_ref_pic,
    .offset_for_top_to_bottom_field = sps->offset_for_top_to_bottom_field,
    .num_ref_frames_in_pic_order_cnt_cycle = sps->num_ref_frames_in_pic_order_cnt_cycle,
    .max_num_ref_frames = static_cast<uint8_t>(sps->num_ref_frames),
    .pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1,
    .pic_height_in_map_units_minus1 = sps->pic_height_in_map_units_minus1,
    .frame_crop_left_offset = sps->frame_crop_left_offset,
    .frame_crop_right_offset = sps->frame_crop_right_offset,
    .frame_crop_top_offset = sps->frame_crop_top_offset,
    .frame_crop_bottom_offset = sps->frame_crop_bottom_offset,
    .flags = {
      .constraint_set0_flag = sps->constraint_set0_flag,
      .constraint_set1_flag = sps->constraint_set1_flag,
      .constraint_set2_flag = sps->constraint_set2_flag,
      .constraint_set3_flag = sps->constraint_set3_flag,
      .constraint_set4_flag = sps->constraint_set4_flag,
      .constraint_set5_flag = sps->constraint_set4_flag,
      .direct_8x8_inference_flag = sps->direct_8x8_inference_flag,
      .mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag,
      .frame_mbs_only_flag = sps->frame_mbs_only_flag,
      .delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag,
      .gaps_in_frame_num_value_allowed_flag =
          sps->gaps_in_frame_num_value_allowed_flag,
      .qpprime_y_zero_transform_bypass_flag =
          sps->qpprime_y_zero_transform_bypass_flag,
      .frame_cropping_flag = sps->frame_cropping_flag,
      .seq_scaling_matrix_present_flag = sps->scaling_matrix_present_flag,
      .vui_parameters_present_flag = sps->vui_parameters_present_flag,
    },
    /* .offset_for_ref_frame[255] = filled later */
    /* .pScalingLists = filled later */
    /* .pSequenceParameterSetVui = filled later */
  };

  VkParserH264PictureData* h264 = &vkpic->data.CodecSpecific.h264;

  h264->pStdSps = &std_sps;
  h264->CurrFieldOrderCnt[0] = slice->header.delta_pic_order_cnt[0];
  h264->CurrFieldOrderCnt[1] = slice->header.delta_pic_order_cnt[1];

  return GST_FLOW_OK;
}

static void
gst_h264_dec_set_property(GObject * object, guint property_id, const GValue * value, GParamSpec *pspec)
{
  GstH264Dec *self = GST_H264_DEC(object);

  switch (property_id) {
    case PROP_USER_DATA:
      self->client = reinterpret_cast<VkParserVideoDecodeClient *>(g_value_get_pointer(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
gst_h264_dec_class_init(GstH264DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
  gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));

  gobject_class->set_property = gst_h264_dec_set_property;

  h264decoder_class->new_sequence = gst_h264_dec_new_sequence;
  h264decoder_class->decode_slice = gst_h264_dec_decode_slice;
  h264decoder_class->new_picture = gst_h264_dec_new_picture;
  h264decoder_class->output_picture = gst_h264_dec_output_picture;
  h264decoder_class->start_picture = gst_h264_dec_start_picture;
  h264decoder_class->end_picture = gst_h264_dec_end_picture;
  h264decoder_class->new_field_picture = gst_h264_dec_new_field_picture;

  g_object_class_install_property(gobject_class, PROP_USER_DATA,
      g_param_spec_pointer("user-data", "user-data", "user-data", GParamFlags(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));
}

static void
gst_h264_dec_init(GstH264Dec * self)
{
  gst_h264_decoder_set_process_ref_pic_lists(GST_H264_DECODER(self), FALSE);
}
