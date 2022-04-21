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
};

enum {
  PROP_USER_DATA = 1,
};

G_DEFINE_FINAL_TYPE(GstH264Dec, gst_h264_dec, GST_TYPE_H264_DECODER)

static gpointer parent_class = NULL;

static GstFlowReturn
gst_h264_dec_new_sequence(GstH264Decoder * decoder, const GstH264SPS * sps, gint max_dpb_size)
{
  GstH264Dec *self = GST_H264_DEC (decoder);
  GstVideoDecoder *dec = GST_VIDEO_DECODER (decoder);
  GstVideoCodecState *state;
  VkParserSequenceInfo seqInfo = { };

  seqInfo.eCodec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;
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
  seqInfo.nMaxWidth = sps->width;
  seqInfo.nMaxHeight = sps->height;
  seqInfo.nChromaFormat = sps->chroma_format_idc;         // Chroma Format (0=4:0:0, 1=4:2:0, 2=4:2:2, 3=4:4:4)
  seqInfo.uBitDepthLumaMinus8 = sps->bit_depth_luma_minus8; // Luma bit depth (0=8bit)
  seqInfo.uBitDepthChromaMinus8 = sps->bit_depth_chroma_minus8; // Chroma bit depth (0=8bit)
  if (sps->vui_parameters_present_flag) {
    seqInfo.uVideoFullRange = sps->vui_parameters.video_full_range_flag;       // 0=16-235, 1=0-255
    if (sps->vui_parameters.nal_hrd_parameters_present_flag)
         seqInfo.lBitrate = sps->vui_parameters.nal_hrd_parameters.bit_rate_scale;
    else if (sps->vui_parameters.vcl_hrd_parameters_present_flag)
         seqInfo.lBitrate = sps->vui_parameters.vcl_hrd_parameters.bit_rate_scale;
    seqInfo.lDARWidth = sps->vui_parameters.par_n;
    seqInfo.lDARHeight = sps->vui_parameters.par_d;
    seqInfo.lVideoFormat = sps->vui_parameters.video_format; // Video Format (VideoFormatXXX)
    seqInfo.lColorPrimaries = sps->vui_parameters.colour_primaries; // Colour Primaries (ColorPrimariesXXX)
    seqInfo.lTransferCharacteristics = sps->vui_parameters.transfer_characteristics;
    seqInfo.lMatrixCoefficients = sps->vui_parameters.matrix_coefficients;
  }
  //  int32_t cbSequenceHeader; // Number of bytes in SequenceHeaderData
  seqInfo.nMinNumDecodeSurfaces = max_dpb_size;
  //  uint8_t SequenceHeaderData[1024]; // Raw sequence header data (codec-specific)
  seqInfo.pbSideData = nullptr;
  seqInfo.cbSideData = 0;

  if (self->client)
    self->client->BeginSequence (&seqInfo);

  state = gst_video_decoder_set_output_state (dec, GST_VIDEO_FORMAT_NV12, seqInfo.nDisplayWidth, seqInfo.nDisplayHeight, decoder->input_state);
  gst_video_codec_state_unref (state);

  gst_video_decoder_negotiate (dec);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_decode_slice(GstH264Decoder * decoder, GstH264Picture * picture, GstH264Slice * slice, GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_picture(GstH264Decoder * decoder, GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  frame->output_buffer = gst_buffer_new();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_field_picture(GstH264Decoder * decoder, GstH264Picture * first_field, GstH264Picture * second_field)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_output_picture(GstH264Decoder * decoder, GstVideoCodecFrame * frame, GstH264Picture * picture)
{
  return gst_video_decoder_finish_frame(GST_VIDEO_DECODER (decoder), frame);
}

static GstFlowReturn
gst_h264_dec_end_picture(GstH264Decoder * decoder, GstH264Picture * picture)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_start_picture(GstH264Decoder * decoder, GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
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
