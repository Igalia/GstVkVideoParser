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

#include "pipeline.h"

#include <gst/check/gstharness.h>

#include "h264dec.h"

struct _GstVideoParser
{
  GstObject parent;
  gpointer user_data;
  VkVideoCodecOperationFlagBitsKHR codec;
  gboolean oob_pic_params;
  GstHarness *parser, *decoder;
};

enum {
  PROP_USER_DATA = 1,
  PROP_CODEC,
  PROP_OOB_PIC_PARAMS,
};

GST_DEBUG_CATEGORY(gst_video_parser_debug);
#define GST_CAT_DEFAULT gst_video_parser_debug

G_DEFINE_FINAL_TYPE_WITH_CODE (GstVideoParser, gst_video_parser, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_video_parser_debug, "videoparser", 0, "Video Parser"))

#include <sys/syscall.h>
#include <unistd.h>

static void
gst_video_parser_dispose (GObject* object)
{
  GstVideoParser *self = GST_VIDEO_PARSER (object);

  gst_harness_teardown (self->parser);

  G_OBJECT_CLASS (gst_video_parser_parent_class)->dispose (object);
}

static void
gst_video_parser_constructed (GObject * object)
{
  GstVideoParser *self = GST_VIDEO_PARSER (object);
  GstElement *decoder;
  GstHarness *sink;
  const char *parser_name = NULL;

  if (self->codec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT) {
    parser_name = "h264parse";
    decoder = g_object_new (GST_TYPE_H264_DEC, "user-data", self->user_data,
        "oob-pic-params", self->oob_pic_params, NULL);
    g_assert (decoder);
  } else {
    g_assert (FALSE);
  }

  self->parser = gst_harness_new (parser_name);
  sink = gst_harness_new_with_element (decoder, "sink", "src");

  gst_harness_set_live (self->parser, FALSE);
  gst_harness_add_sink_harness (self->parser, sink);
  gst_harness_set_drop_buffers (sink, TRUE);
  //gst_harness_set_blocking_push_mode (self->parser);

  gst_harness_set_caps_str (self->parser,
      "video/x-h264,parsed=false",
      "video/x-h264,stream-format=byte-stream,alignment=nal,parsed=true");
}

static void
gst_video_parser_set_property (GObject * object, guint property_id,
     const GValue * value, GParamSpec *pspec)
{
  GstVideoParser *self = GST_VIDEO_PARSER (object);

  switch (property_id) {
    case PROP_USER_DATA:
      self->user_data = g_value_get_pointer (value);
      break;
    case PROP_CODEC:
      self->codec = g_value_get_uint (value);
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
gst_video_parser_class_init (GstVideoParserClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_video_parser_constructed;
  gobject_class->dispose = gst_video_parser_dispose;
  gobject_class->set_property = gst_video_parser_set_property;

  g_object_class_install_property (gobject_class, PROP_USER_DATA,
      g_param_spec_pointer ("user-data", "user-data", "user-data",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class, PROP_CODEC,
      g_param_spec_uint ("codec", "codec", "codec",
          VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT,
          VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT,
          VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class,
      PROP_OOB_PIC_PARAMS, g_param_spec_boolean ("oob-pic-params",
      "oob-pic-params", "oop-pic-params", FALSE,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_video_parser_init (GstVideoParser * self)
{
}

GstVideoParser *
gst_video_parser_new (gpointer user_data,
    VkVideoCodecOperationFlagBitsKHR codec, gboolean oob_pic_params)
{
  return GST_VIDEO_PARSER (g_object_new (GST_TYPE_VIDEO_PARSER, "user-data",
      user_data, "codec", codec, "oob-pic-params", oob_pic_params, NULL));
}

GstFlowReturn
gst_video_parser_push_buffer (GstVideoParser * self, GstBuffer * buffer)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (self, "Pushing buffer: %" GST_PTR_FORMAT, buffer);

  ret = gst_harness_push (self->parser, buffer);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS) {
    GST_WARNING_OBJECT (self, "Couldn't push buffer: %s", gst_flow_get_name (ret));
    return ret;
  }

  while ((buf = gst_harness_try_pull (self->parser))) {
    GST_DEBUG_OBJECT (self, "Pushing buffer to decoder: %" GST_PTR_FORMAT, buf);
    ret = gst_harness_push (self->parser->sink_harness, buf);
    if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS) {
      GST_WARNING_OBJECT (self, "Couldn't push buffer: %s", gst_flow_get_name (ret));
      return ret;
    }
  }

  return ret;
}

GstFlowReturn
gst_video_parser_eos (GstVideoParser * self)
{
  GstBuffer *buffer = NULL;

  GST_DEBUG_OBJECT (self, "Pushing EOS");

  if (!gst_harness_push_event (self->parser, gst_event_new_eos ()))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}
