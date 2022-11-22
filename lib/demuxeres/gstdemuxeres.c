/* DemuxerES
 * Copyright (C) 2022 Igalia, S.L.
 *     Author: Stephane Cerveau <scerveau@igalia.com>
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

#include "gstdemuxeres.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

static guint packet_counter = 0;

GST_DEBUG_CATEGORY_STATIC (demuxer_es_debug);
#define GST_CAT_DEFAULT demuxer_es_debug

typedef enum _GstDemuxerESState
{
  DEMUXER_ES_STATE_IDLE = 0,
  DEMUXER_ES_STATE_READY,
  DEMUXER_ES_STATE_ERROR,
  DEMUXER_ES_STATE_EOS,
} GstDemuxerESState;

struct _GstDemuxerESPrivate
{
  GstElement *src;
  GstElement *pipeline;
  GstElement *parsebin;
  gchar *current_uri;

  GAsyncQueue *packets;
  GList *streams;

  GstDemuxerESState state;
  GCond ready_cond;
  GMutex ready_mutex;
  GMutex queue_mutex;
};

static gchar *
get_gst_valid_uri (const gchar * filename)
{
  if (gst_uri_is_valid (filename)) {
    return g_strdup (filename);
  }

  return gst_filename_to_uri (filename, NULL);
}

struct _GstDemuxerESPacketPrivate
{
  GstSample *sample;
};

static GstFlowReturn
appsink_new_sample_cb (GstAppSink * appsink, gpointer user_data)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstDemuxerEStream *stream = user_data;
  GstDemuxerES *demuxer = stream->demuxer;
  GstDemuxerESPrivate *priv = demuxer->priv;

  sample = gst_app_sink_pull_sample (appsink);
  buffer = gst_sample_get_buffer (sample);
  if (buffer) {
    GstDemuxerESPacket *packet = g_new0 (GstDemuxerESPacket, 1);
    packet->priv = g_new0 (GstDemuxerESPacketPrivate, 1);
    packet->priv->sample = sample;
    GstMapInfo mi;
    gst_buffer_map (buffer, &mi, GST_MAP_READ);
    packet->data = mi.data;
    packet->data_size = mi.size;
    gst_buffer_unmap (buffer, &mi);
    packet->stream_type = stream->type;
    packet->stream_id = stream->id;
    packet_counter++;
    packet->packet_number = packet_counter;
    packet->pts = GST_BUFFER_PTS (buffer);
    packet->dts = GST_BUFFER_DTS (buffer);
    packet->duration = GST_BUFFER_DURATION (buffer);
    g_mutex_lock (&priv->queue_mutex);
    g_async_queue_push (priv->packets, packet);
    g_mutex_unlock (&priv->queue_mutex);
  }

  return GST_FLOW_OK;
}

static GstDemuxerEStreamType
gst_parse_stream_get_type_from_pad (GstPad * pad)
{
  GstCaps *caps;
  const GstStructure *s;
  const gchar *name;
  GstDemuxerEStreamType type = DEMUXER_ES_STREAM_TYPE_UNKNOWN;

  caps = gst_pad_query_caps (pad, NULL);


  if (caps) {
    s = gst_caps_get_structure (caps, 0);
    name = gst_structure_get_name (s);
    if (g_str_has_prefix (name, "video")) {
      type = DEMUXER_ES_STREAM_TYPE_VIDEO;
    } else if (g_str_has_prefix (name, "audio")) {
      type = DEMUXER_ES_STREAM_TYPE_AUDIO;
    } else if (g_str_has_prefix (name, "text")) {
      type = DEMUXER_ES_STREAM_TYPE_TEXT;
    }
    gst_caps_unref (caps);
  }
  return type;
}

static GstDemuxerVideoCodec
gst_parse_stream_get_vcodec_from_caps (GstCaps * caps)
{
  GstDemuxerVideoCodec ret = DEMUXER_ES_VIDEO_CODEC_UNKNOWN;
  GstStructure *s;
  const gchar *name;

  if (!caps)
    return ret;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    goto beach;

  name = gst_structure_get_name (s);
  if (!name)
    goto beach;

  if (!strcmp (name, "video/x-h264")) {
    ret = DEMUXER_ES_VIDEO_CODEC_H264;
  } else if (!strcmp (name, "video/x-h265")) {
    ret = DEMUXER_ES_VIDEO_CODEC_H265;
  }

beach:
  return ret;
}

static GstDemuxerAudioCodec
gst_parse_stream_get_acodec_from_caps (GstCaps * caps)
{
  GstDemuxerAudioCodec ret = DEMUXER_ES_AUDIO_CODEC_UNKNOWN;
  GstStructure *s;
  const gchar *name;

  if (!caps)
    return ret;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    goto beach;

  name = gst_structure_get_name (s);
  if (!name)
    goto beach;

  if (!strcmp (name, "audio/x-aac")) {
    ret = DEMUXER_ES_AUDIO_CODEC_AAC;
  }

beach:
  return ret;
}

void
gst_parse_stream_teardown (GstDemuxerEStream * stream)
{
  switch (stream->type) {
    case DEMUXER_ES_STREAM_TYPE_VIDEO:
      g_free (stream->info.video.profile);
      g_free (stream->info.video.level);
    default:
      break;
  }
  g_free (stream);
}

GstDemuxerEStream *
gst_parse_stream_create (GstDemuxerES * demuxer, GstPad * pad)
{
  GstDemuxerEStream *stream;
  GstElement *appsink;
  GstPad *sinkpad;
  GstDemuxerESPrivate *priv = demuxer->priv;
  GstCaps *caps;
  GstAppSinkCallbacks callbacks = { 0, };

  stream = g_new0 (GstDemuxerEStream, 1);
  stream->demuxer = demuxer;
  stream->type = gst_parse_stream_get_type_from_pad (pad);
  caps = gst_pad_query_caps (pad, NULL);
  if (caps) {
    gchar* caps_desc = gst_caps_to_string(caps);
    GST_ERROR ("the stream caps is %s",caps_desc);
    g_free (caps_desc);
    GstStructure *s =
        gst_caps_is_empty (caps) ? NULL : gst_caps_get_structure (caps, 0);
    if (s) {
      switch (stream->type) {
        case DEMUXER_ES_STREAM_TYPE_VIDEO:
        {
          const GValue *fps, *par;
          gst_structure_get_int (s, "width", &stream->info.video.width);
          gst_structure_get_int (s, "height", &stream->info.video.height);
          gst_structure_get_int (s, "bitrate", &stream->info.video.bitrate);
          stream->info.video.vcodec =
              gst_parse_stream_get_vcodec_from_caps (caps);
          par = gst_structure_get_value (s, "pixel-aspect-ratio");
          if (par && GST_VALUE_HOLDS_FRACTION (par)) {
            stream->info.video.par_n = gst_value_get_fraction_numerator (par);
            stream->info.video.par_d = gst_value_get_fraction_denominator (par);
          } else {
            stream->info.video.par_n = stream->info.video.par_d = 1;
          }
          fps = gst_structure_get_value (s, "framerate");
          if (fps && GST_VALUE_HOLDS_FRACTION (fps)) {
            stream->info.video.fps_n = gst_value_get_fraction_numerator (fps);
            stream->info.video.fps_d = gst_value_get_fraction_denominator (fps);
          } else {
            stream->info.video.fps_n = 30;
            stream->info.video.fps_d = 1;
          }

          stream->info.video.profile =
              g_strdup (gst_structure_get_string (s, "profile"));
          stream->info.video.level =
              g_strdup (gst_structure_get_string (s, "level"));
          break;
        }
        case DEMUXER_ES_STREAM_TYPE_AUDIO:
        {
          gst_structure_get_int (s, "channels", &stream->info.audio.channels);
          gst_structure_get_int (s, "bitrate", &stream->info.audio.bitrate);
          gst_structure_get_int (s, "rate", &stream->info.audio.rate);
          gst_structure_get_int (s, "width", &stream->info.audio.width);
          gst_structure_get_int (s, "depth", &stream->info.audio.depth);
          stream->info.audio.acodec =
              gst_parse_stream_get_acodec_from_caps (caps);
          break;
        }
        default:
          break;
      }
    }
  }

  gst_caps_unref (caps);

  stream->id = g_list_length (priv->streams);
  appsink = gst_element_factory_make ("appsink", NULL);
  g_object_set (appsink, "sync", FALSE, "emit-signals", TRUE, NULL);
  gst_bin_add_many (GST_BIN (priv->pipeline), appsink, NULL);
  sinkpad = gst_element_get_static_pad (appsink, "sink");
  gst_pad_link (pad, sinkpad);
  gst_element_sync_state_with_parent (appsink);

  callbacks.new_sample =appsink_new_sample_cb;
  gst_app_sink_set_callbacks ((GstAppSink *) appsink, &callbacks, stream,
      NULL);

  return stream;
}

static inline gboolean
wait_for_demuxer_ready (GstDemuxerES * demuxer, gint64 wait_time)
{
  GstDemuxerESPrivate *priv = demuxer->priv;
  gboolean success = FALSE;
  gint64 end_time = g_get_monotonic_time () + wait_time;

  g_mutex_lock (&priv->ready_mutex);
  while (!success) {

    success = g_cond_wait_until (&priv->ready_cond,
        &priv->ready_mutex, end_time);
    GST_DEBUG ("After wait success %d", success);

    priv->state = success ? DEMUXER_ES_STATE_READY : DEMUXER_ES_RESULT_ERROR;
    success = TRUE;
  }
  g_mutex_unlock (&priv->ready_mutex);

  return (priv->state == DEMUXER_ES_STATE_READY);
}

static inline void
set_demuxer_ready (GstDemuxerES * demuxer)
{
  GstDemuxerESPrivate *priv = demuxer->priv;

  g_mutex_lock (&priv->ready_mutex);
  g_cond_signal (&priv->ready_cond);
  g_mutex_unlock (&priv->ready_mutex);
}

static void
parsebin_pad_added_cb (GstElement * parsebin, GstPad * pad,
    GstDemuxerES * demuxer)
{
  GstDemuxerEStream *stream = NULL;
  GstDemuxerESPrivate *priv = demuxer->priv;

  if (!GST_PAD_IS_SRC (pad))
    return;


  GST_DEBUG ("pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  stream = gst_parse_stream_create (demuxer, pad);
  priv->streams = g_list_append (priv->streams, stream);

  GST_DEBUG ("Done linking");
  if (priv->state == DEMUXER_ES_STATE_IDLE)
    set_demuxer_ready (demuxer);
}

static void
parsebin_pad_no_more_pads (GstElement * parsebin, GstDemuxerES * demuxer)
{
  GST_ERROR ("No more pads received from parsebin");
}

static void
urisourcebin_pad_added_cb (GstElement * parsebin, GstPad * pad,
    GstDemuxerES * demuxer)
{
  if (!GST_PAD_IS_SRC (pad))
    return;
  GST_DEBUG ("pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  if (!gst_element_link_many (demuxer->priv->src, demuxer->priv->parsebin, NULL)) {
    GST_ERROR ("failed linking");
    gst_demuxer_es_teardown (demuxer);
    demuxer = NULL;
  }
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, GstDemuxerES * demuxer)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      demuxer->priv->state = DEMUXER_ES_STATE_ERROR;
      break;
    }
    case GST_MESSAGE_EOS:
    {
      demuxer->priv->state = DEMUXER_ES_STATE_EOS;
      break;
    }
    default:
      GST_DEBUG ("message received %s", GST_MESSAGE_TYPE_NAME (message));
      break;
  }

  return TRUE;
}

static gboolean
check_for_bus_message (GstDemuxerES * demuxer)
{
  GstDemuxerESPrivate *priv = demuxer->priv;
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
  gboolean check_message = TRUE;
  gboolean terminate = FALSE;
  while (check_message) {
    GstMessage *message = gst_bus_timed_pop (bus, 1);
    if (G_LIKELY (message)) {
      terminate = handle_bus_message (bus, message, demuxer);
      gst_message_unref (message);
    } else
      check_message = FALSE;
  }
  gst_object_unref (bus);
  return terminate;

}

GstDemuxerES *
gst_demuxer_es_new (const gchar * uri)
{
  GstDemuxerES *demuxer;
  GstDemuxerESPrivate *priv;

  GstStateChangeReturn sret;

  if (!gst_init_check (NULL, NULL, NULL))
    return NULL;

  GST_DEBUG_CATEGORY_INIT (demuxer_es_debug, "demuxeres",
      0, "demuxeres");

  demuxer = g_new0 (GstDemuxerES, 1);
  g_assert (demuxer != NULL);
  demuxer->priv = g_new0 (GstDemuxerESPrivate, 1);
  priv = demuxer->priv;
  g_mutex_init (&priv->queue_mutex);
  g_mutex_init (&priv->ready_mutex);
  g_cond_init (&priv->ready_cond);

  priv->current_uri = get_gst_valid_uri(uri);

  priv->packets = g_async_queue_new_full ((GDestroyNotify) g_free);

  priv->pipeline = gst_pipeline_new ("demuxeres");


  priv->src = gst_element_factory_make ("urisourcebin", NULL);
  g_object_set (G_OBJECT (priv->src), "priv->current_uri", uri, NULL);

  g_signal_connect (priv->src, "pad-added",
      G_CALLBACK (urisourcebin_pad_added_cb), demuxer);


  priv->parsebin = gst_element_factory_make ("parsebin", NULL);

  g_signal_connect (priv->parsebin, "pad-added",
      G_CALLBACK (parsebin_pad_added_cb), demuxer);
  g_signal_connect (priv->parsebin, "no-more-pads",
      G_CALLBACK (parsebin_pad_no_more_pads), demuxer);

  gst_bin_add_many (GST_BIN (priv->pipeline), priv->src, priv->parsebin, NULL);

  sret = gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      GST_ERROR ("Pipeline failed to go to PLAYING state");
      gst_element_set_state (priv->pipeline, GST_STATE_NULL);
      gst_demuxer_es_teardown (demuxer);
      demuxer = NULL;
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      GST_DEBUG ("Pipeline is live.");
      break;
    case GST_STATE_CHANGE_ASYNC:
      GST_DEBUG ("Prerolling...");
      break;
    default:
      break;
  }
  if (demuxer && !wait_for_demuxer_ready (demuxer, 5 * G_TIME_SPAN_SECOND)) {
    GST_ERROR ("The demuxer never got ready after 5s");
    gst_demuxer_es_teardown (demuxer);
    demuxer = NULL;
  }
  return demuxer;
}

void
gst_demuxer_es_clear_packet (GstDemuxerESPacket * packet)
{
  gst_sample_unref (packet->priv->sample);
  g_free (packet->priv);
  g_free(packet);
}

GstDemuxerESResult
gst_demuxer_es_read_packet (GstDemuxerES * demuxer, GstDemuxerESPacket ** packet)
{
  GstDemuxerESPacket *queued_packet;

  GstDemuxerESPrivate *priv = demuxer->priv;
  GstDemuxerESResult result = DEMUXER_ES_RESULT_NO_PACKET;

  check_for_bus_message (demuxer);
  if (priv->state == DEMUXER_ES_STATE_ERROR) {
    return DEMUXER_ES_RESULT_ERROR;
  }

  queued_packet =
      (GstDemuxerESPacket *) g_async_queue_timeout_pop (priv->packets,
      G_TIME_SPAN_MILLISECOND * 100);

  if (queued_packet) {
    *packet = queued_packet;
    result = DEMUXER_ES_RESULT_NEW_PACKET;
    if (priv->state == DEMUXER_ES_STATE_EOS && g_async_queue_length(priv->packets) == 0) {
      result =  DEMUXER_ES_RESULT_LAST_PACKET;
    }
  } else {
    if (priv->state == DEMUXER_ES_STATE_EOS)
      result = DEMUXER_ES_RESULT_EOS;
  }

  return result;
}

GstDemuxerEStream *
gst_demuxer_es_find_best_stream (GstDemuxerES * demuxer,
    GstDemuxerEStreamType type)
{
  GstDemuxerESPrivate *priv;
  GList *l;

  if (!demuxer)
    return NULL;

  priv = demuxer->priv;

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (priv->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-demuxeres.snapshot");

  if (priv->state == DEMUXER_ES_STATE_IDLE) {
    return NULL;
  }

  for (l = priv->streams; l != NULL; l = g_list_next (l)) {
    GstDemuxerEStream *stream = l->data;
    if (stream->type == type) {
      return stream;
    }
  }

  return NULL;
}

void
gst_demuxer_es_teardown (GstDemuxerES * demuxer)
{
  GstBus *bus;
  GstDemuxerESPrivate *priv = demuxer->priv;
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  bus = gst_element_get_bus (priv->pipeline);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  g_async_queue_unref (priv->packets);
  g_list_free_full (priv->streams, (GDestroyNotify) gst_parse_stream_teardown);
  gst_object_unref (priv->pipeline);

  g_cond_clear (&priv->ready_cond);
  g_mutex_clear (&priv->ready_mutex);
  g_mutex_clear (&priv->queue_mutex);
  g_free (priv->current_uri);
  g_free (priv);
  demuxer->priv = NULL;
  g_free (demuxer);
}
