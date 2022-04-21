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

#define GST_H264_DEC(obj)           ((GstH264Dec *) obj)
#define GST_H264_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstH264DecClass))
#define GST_H264_DEC_CLASS(klass)   ((GstH264DecClass *) klass)

GST_DEBUG_CATEGORY_EXTERN (gst_video_parser_debug);
#define GST_CAT_DEFAULT gst_video_parser_debug

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/x-h264"));

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/x-raw, format=(string)NV12"));

enum
{
  NEW_SEQUENCE,
  NEW_PICTURE,
  END_PICTURE,
  OUT_PICTURE,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _GstH264Dec GstH264Dec;
struct _GstH264Dec
{
  GstH264Decoder parent;
};

G_DEFINE_FINAL_TYPE (GstH264Dec, gst_h264_dec, GST_TYPE_H264_DECODER)

static gpointer *parent_class = NULL;

static GstFlowReturn
gst_h264_dec_new_sequence (GstH264Decoder * decoder, const GstH264SPS * sps,
    gint max_dpb_size)
{
  GstH264Dec *self = GST_H264_DEC (decoder);
  GstVideoDecoder *dec = GST_VIDEO_DECODER (decoder);
  GstVideoCodecState *state;

  g_signal_emit (self, signals[NEW_SEQUENCE], 0, sps);

  state = gst_video_decoder_set_output_state (dec, GST_VIDEO_FORMAT_NV12, 16, 16, decoder->input_state);
  gst_video_codec_state_unref (state);

  gst_video_decoder_negotiate (dec);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_decode_slice (GstH264Decoder * decoder, GstH264Picture * picture,
    GstH264Slice * slice, GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_picture (GstH264Decoder * decoder, GstVideoCodecFrame * frame,
    GstH264Picture * picture)
{
  GstH264Dec *self = GST_H264_DEC (decoder);

  g_signal_emit (self, signals[NEW_PICTURE], 0);

  frame->output_buffer = gst_buffer_new ();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_new_field_picture (GstH264Decoder * decoder, GstH264Picture * first_field,
    GstH264Picture * second_field)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_output_picture (GstH264Decoder * decoder, GstVideoCodecFrame * frame,
    GstH264Picture * picture)
{
  g_signal_emit (GST_H264_DEC (decoder), signals[OUT_PICTURE], 0);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (decoder), frame);
}

static GstFlowReturn
gst_h264_dec_end_picture (GstH264Decoder * decoder, GstH264Picture * picture)
{
  g_signal_emit (GST_H264_DEC (decoder), signals[END_PICTURE], 0);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_dec_start_picture (GstH264Decoder * decoder, GstH264Picture * picture,
    GstH264Slice * slice, GstH264Dpb * dpb)
{
  return GST_FLOW_OK;
}

static void
gst_h264_dec_class_init (GstH264DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_factory));

  h264decoder_class->new_sequence = gst_h264_dec_new_sequence;
  h264decoder_class->decode_slice = gst_h264_dec_decode_slice;
  h264decoder_class->new_picture = gst_h264_dec_new_picture;
  h264decoder_class->output_picture = gst_h264_dec_output_picture;
  h264decoder_class->start_picture = gst_h264_dec_start_picture;
  h264decoder_class->end_picture = gst_h264_dec_end_picture;
  h264decoder_class->new_field_picture = gst_h264_dec_new_field_picture;

  signals[NEW_SEQUENCE] = g_signal_new ("new-sequence", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
  signals[NEW_PICTURE] = g_signal_new ("new-picture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[END_PICTURE] = g_signal_new ("end-picture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[OUT_PICTURE] = g_signal_new ("out-picture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
gst_h264_dec_init (GstH264Dec * self)
{
  gst_h264_decoder_set_process_ref_pic_lists (GST_H264_DECODER (self), FALSE);
}
