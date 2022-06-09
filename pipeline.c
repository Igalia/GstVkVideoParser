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

#include <gst/app/gstappsrc.h>

#include "h264dec.h"

struct _GstVideoParser
{
  GstObject parent;
  GstElement *pipeline, *appsrc;
  gboolean need_data, got_stream, got_error, got_eos;
  gpointer user_data;
  VkVideoCodecOperationFlagBitsKHR codec;
  gboolean oob_pic_params;
  GstBus *bus;
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

static void
on_pad_added (GstElement* parsebin, GstPad* new_pad, gpointer user_data)
{
  GstVideoParser *self = GST_VIDEO_PARSER (user_data);
  GstCaps *caps;
  GstStructure *st;
  GstElement *decoder, *sink;
  GstPad *decoder_pad, *sink_pad;
  GstPadLinkReturn ret;
  const char *name;

  GST_PAD_STREAM_LOCK (new_pad);
  if (!gst_pad_is_active (new_pad))
    goto bail;

  if (self->got_stream)
    goto bail;

  caps = gst_pad_get_current_caps (new_pad);
  if (!caps)
    caps = gst_pad_query_caps (new_pad, NULL);

  GST_DEBUG_OBJECT (self, "new caps: %" GST_PTR_FORMAT, caps);

  st = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (st);
  gst_caps_unref (caps);

  if (self->codec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT
      && g_strcmp0 (name, "video/x-h264") == 0) {
    GST_DEBUG_OBJECT (self, "H.264 stream found");
    decoder = g_object_new (GST_TYPE_H264_DEC, "user-data", self->user_data,
        "oob-pic-params", self->oob_pic_params, NULL);
    g_assert (decoder);

    gst_bin_add (GST_BIN (self->pipeline), decoder);
    gst_element_sync_state_with_parent (decoder);
    decoder_pad = gst_element_get_static_pad (decoder, "sink");
    ret = gst_pad_link (new_pad, decoder_pad);
    gst_object_unref (decoder_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
      GST_ELEMENT_ERROR (self->pipeline, CORE, PAD, ("Failed to link parserbin to decoder"), (NULL));
      goto bail;
    }

    sink = gst_element_factory_make ("fakesink", NULL);
    g_assert (sink);
    gst_bin_add (GST_BIN (self->pipeline), sink);
    gst_element_sync_state_with_parent (sink);
    sink_pad = gst_element_get_static_pad (sink, "sink");
    decoder_pad = gst_element_get_static_pad (decoder, "src");
    ret = gst_pad_link (decoder_pad, sink_pad);
    gst_object_unref (decoder_pad);
    gst_object_unref (sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
      GST_ELEMENT_ERROR (self->pipeline, CORE, PAD, ("Failed to link decoder to fakesink"), (NULL));
      goto bail;
    }

    self->got_stream = TRUE;
  }

bail:
  GST_PAD_STREAM_UNLOCK (new_pad);
}

static void
process_messages (GstVideoParser * self)
{
  GstMessage *msg;
  
  if (!self->bus)
    self->bus = gst_pipeline_get_bus (GST_PIPELINE (self->pipeline));

  while (TRUE) {
    msg = gst_bus_timed_pop (self->bus, 0.1 * GST_SECOND);
    if (!msg)
      return;

    GST_DEBUG_OBJECT (self, "%s", GST_MESSAGE_TYPE_NAME (msg));

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:{
        GError *err = NULL;
        char *debug = NULL;

        gst_message_parse_error (msg, &err, &debug);
        GST_ERROR_OBJECT (self, "Error: %s - %s", err->message, debug);
        g_clear_error (&err);
        g_free (debug);

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "videoparse.error");

        self->got_error = TRUE;
        break;
      }

    case GST_MESSAGE_EOS:
        self->got_eos = TRUE;
        break;

      case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT_CAST (self->pipeline)) {
          GstState old, snew;
          char *name;

          gst_message_parse_state_changed (msg, &old, &snew, NULL);
          name = g_strdup_printf ("videoparse.%s_%s", gst_element_state_get_name (old),
            gst_element_state_get_name (snew));

          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, name);

          g_free (name);

          break;
        }

      default:
        break;
    }

    gst_message_unref (msg);
  }
}

static void
gst_video_parser_dispose (GObject* object)
{
  GstVideoParser *self = GST_VIDEO_PARSER (object);
  GstStateChangeReturn ret;

  ret = gst_element_set_state (self->pipeline, GST_STATE_NULL);
  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_WARNING_OBJECT (self, "Failed to change to NULL state");

  gst_clear_object (&self->bus);
  gst_clear_object (&self->pipeline);

  G_OBJECT_CLASS (gst_video_parser_parent_class)->dispose (object);
}

static void
need_data (GstAppSrc * src, guint length, gpointer user_data)
{
  GstVideoParser *self = GST_VIDEO_PARSER (user_data);

  GST_DEBUG_OBJECT (self, "Need data");

  g_atomic_int_set (&self->need_data, TRUE);
}

static void
enough_data (GstAppSrc * src, gpointer user_data)
{
  GstVideoParser *self = GST_VIDEO_PARSER (user_data);

  GST_DEBUG_OBJECT (self, "Enough data");

  g_atomic_int_set (&self->need_data, FALSE);
}

static void
gst_video_parser_constructed (GObject * object)
{
  GstVideoParser *self = GST_VIDEO_PARSER (object);
  GstElement *parsebin;
  GstStateChangeReturn ret;
  GstAppSrcCallbacks cb = {
    need_data, enough_data, NULL,
  };

  self->appsrc = gst_element_factory_make ("appsrc", NULL);
  g_assert (self->appsrc);
  gst_app_src_set_callbacks (GST_APP_SRC (self->appsrc), &cb, self, NULL);

  parsebin = gst_element_factory_make ("parsebin", NULL);
  g_assert (parsebin);

  g_signal_connect (parsebin, "pad-added", G_CALLBACK (on_pad_added), self);

  self->pipeline = gst_pipeline_new ("videoparse");
  gst_bin_add_many (GST_BIN (self->pipeline), self->appsrc, parsebin, NULL);
  if (!gst_element_link (self->appsrc, parsebin))
    GST_WARNING_OBJECT (self, "Failed to link appsrc with parsebin");

  ret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_WARNING_OBJECT(self, "Failed to change to PLAYING state");
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

static inline void
gst_video_parser_reset (GstVideoParser * self)
{
  GstStateChangeReturn ret;

  self->got_eos = self->got_error = self->need_data = self->got_stream = FALSE;

  ret = gst_element_set_state (self->pipeline, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_WARNING_OBJECT (self, "Change state to ready might failed");

  ret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_WARNING_OBJECT (self, "Change state to playing might failed");
}

GstFlowReturn
gst_video_parser_push_buffer (GstVideoParser * self, GstBuffer * buffer)
{
  GstFlowReturn ret;

  if (self->got_error)
    return GST_FLOW_ERROR;
  if (self->got_eos)
    return GST_FLOW_EOS;

  GST_DEBUG_OBJECT (self, "Pushing buffer: %" GST_PTR_FORMAT, buffer);

  ret = gst_app_src_push_buffer (GST_APP_SRC (self->appsrc), buffer);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS)
    GST_WARNING_OBJECT (self, "Couldn't push buffer: %s", gst_flow_get_name (ret));

  while (!g_atomic_int_get (&self->need_data)) {
    process_messages (self);

    if (self->got_error)
      return GST_FLOW_ERROR;
    if (self->got_eos)
      return GST_FLOW_EOS;
  }

  return ret;
}

GstFlowReturn
gst_video_parser_eos (GstVideoParser * self)
{
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (self, "Pushing EOS");

  if (self->got_error)
    return GST_FLOW_ERROR;
  if (self->got_eos)
    return GST_FLOW_EOS;

  ret = gst_app_src_end_of_stream (GST_APP_SRC (self->appsrc));
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Couldn't push EOS: %s", gst_flow_get_name (ret));
    return ret;
  }

  while (!self->got_eos) {
    process_messages (self);
    if (self->got_error)
      return GST_FLOW_ERROR;
    if (self->got_eos)
      ret = GST_FLOW_EOS;
  }

  gst_video_parser_reset (self);

  return ret;
}
