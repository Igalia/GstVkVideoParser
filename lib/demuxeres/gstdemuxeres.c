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


typedef enum
{
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP
} GstAutoplugSelectResult;

typedef enum _GstDemuxerESState
{
  DEMUXER_ES_STATE_IDLE = 0,
  DEMUXER_ES_STATE_ERROR,
  DEMUXER_ES_STATE_READY,
  DEMUXER_ES_STATE_EOS,
} GstDemuxerESState;

struct _GstDemuxerESPrivate
{
  GstElement *pipeline;
  gchar *current_uri;

  GAsyncQueue *packets;
  GList *streams;

  GstDemuxerESState state;
  GCond ready_cond;
  GMutex ready_mutex;
  gboolean waiting_del;
  GCond queue_item_del;
  GMutex queue_mutex;
  gint max_queue_size;
  gint threshold_queue_size;
  GThread *bus_thread;
  gboolean bus_exit;
};

#define GST_QUEUE_SIGNAL_DEL(q) G_STMT_START {                          \
  if (q->waiting_del) {                                                 \
    g_cond_signal (&q->queue_item_del);                                        \
  }                                                                     \
} G_STMT_END

#define GST_QUEUE_WAIT_DEL_CHECK(q) G_STMT_START {         \
  q->waiting_del = TRUE;                                                \
  GST_LOG("Wait for the queue to empty.. %d", g_async_queue_length(priv->packets)); \
  g_cond_wait (&q->queue_item_del, &q->queue_mutex);                              \
  q->waiting_del = FALSE;                                               \
} G_STMT_END


static gchar *
get_gst_valid_uri (const gchar * filename) {

  if (gst_uri_is_valid (filename)) {
    return g_strdup (filename);
  }

  return gst_filename_to_uri (filename, NULL);
}

struct _GstDemuxerESPacketPrivate
{
  GstSample *sample;
};

static gboolean
queue_is_filled (GstDemuxerESPrivate * priv, gboolean threshold)
{
  gint queue_size, max_queue_size;
  queue_size = g_async_queue_length (priv->packets);
  GST_LOG ("Queue size: %d", queue_size);
  max_queue_size =
      priv->max_queue_size - (threshold ? priv->threshold_queue_size : 0);
  GST_LOG ("Queue size: %d Max queue size: %d", queue_size, max_queue_size);
  return (queue_size >= max_queue_size);
}

static inline void
set_demuxer_state (GstDemuxerES * demuxer, GstDemuxerESState state)
{
  GstDemuxerESPrivate *priv = demuxer->priv;
  g_mutex_lock (&priv->ready_mutex);
  GST_LOG ("Set state %d", state);
  priv->state = state;
  g_cond_signal (&priv->ready_cond);
  g_mutex_unlock (&priv->ready_mutex);
}

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

    if (queue_is_filled (priv, FALSE)) {
      GST_QUEUE_WAIT_DEL_CHECK (priv);
    }
    GST_LOG ("Pushing a buffer %d", packet->packet_number);
    g_async_queue_push (priv->packets, packet);
    if (priv->state == DEMUXER_ES_STATE_IDLE) {
      set_demuxer_state (demuxer, DEMUXER_ES_STATE_READY);
    }
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
      g_free (stream->data.video.profile);
      g_free (stream->data.video.level);
    default:
      break;
  }
  g_free (stream);
}

GstDemuxerEStream *
gst_parse_stream_create (GstDemuxerES * demuxer, GstPad * pad)
{
  GstDemuxerEStream *stream;
  GstElement *appsink = NULL;
  GstAppSinkCallbacks callbacks = { 0, };
  GstPad *sinkpad;
  GstDemuxerESPrivate *priv = demuxer->priv;
  GstPadLinkReturn link;
  GstCaps *caps = gst_pad_query_caps (pad, NULL);

  if (!caps) {
    GST_ERROR
        ("Unable to get the caps from pad, unable to create a new stream");
    return NULL;
  }
  GST_INFO ("the stream caps is %" GST_PTR_FORMAT, caps);

  stream = g_new0 (GstDemuxerEStream, 1);
  stream->demuxer = demuxer;
  stream->type = gst_parse_stream_get_type_from_pad (pad);

  GstStructure *s = gst_caps_get_structure (caps, 0);
  switch (stream->type) {
    case DEMUXER_ES_STREAM_TYPE_VIDEO:
    {
      gst_video_info_from_caps (&stream->data.video.info, caps);
      gst_structure_get_int (s, "bitrate", &stream->data.video.bitrate);
      stream->data.video.profile =
          g_strdup (gst_structure_get_string (s, "profile"));
      stream->data.video.level =
          g_strdup (gst_structure_get_string (s, "level"));
      stream->data.video.vcodec = gst_parse_stream_get_vcodec_from_caps (caps);
      break;
    }
    case DEMUXER_ES_STREAM_TYPE_AUDIO:
    {
      gst_audio_info_from_caps (&stream->data.audio.info, caps);
      gst_structure_get_int (s, "bitrate", &stream->data.audio.bitrate);
      stream->data.audio.acodec = gst_parse_stream_get_acodec_from_caps (caps);
      break;
    }
    default:
      break;
  }
  stream->id = g_list_length (priv->streams);

  appsink = gst_element_factory_make ("appsink", NULL);
  g_object_set (appsink, "sync", FALSE, "emit-signals", TRUE, NULL);

  gst_bin_add_many (GST_BIN (priv->pipeline), appsink, NULL);
  sinkpad = gst_element_get_static_pad (appsink, "sink");
  if ((link = gst_pad_link (pad, sinkpad)) != GST_PAD_LINK_OK) {
    GST_ERROR ("Unable to link the appsink with the parser link status %d",
        link);
    gst_parse_stream_teardown (stream);
    stream = NULL;
  }

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (priv->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-demuxerer.stream_create");

  if (appsink)
    gst_element_sync_state_with_parent (appsink);

  callbacks.new_sample = appsink_new_sample_cb;
  gst_app_sink_set_callbacks ((GstAppSink *) appsink, &callbacks, stream, NULL);

  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  return stream;
}

static inline gboolean
wait_for_demuxer_ready (GstDemuxerES * demuxer)
{
  GstDemuxerESPrivate *priv = demuxer->priv;

  g_mutex_lock (&priv->ready_mutex);
  while (priv->state == DEMUXER_ES_STATE_IDLE) {
    g_cond_wait (&priv->ready_cond, &priv->ready_mutex);
  }
  g_mutex_unlock (&priv->ready_mutex);

  return (priv->state >= DEMUXER_ES_STATE_READY);
}

static void
uridecodebin_pad_added_cb (GstElement * uridecodebin, GstPad * pad,
    GstDemuxerES * demuxer)
{
  GstDemuxerEStream *stream = NULL;

  if (!GST_PAD_IS_SRC (pad))
    return;

  GST_DEBUG ("pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  stream = gst_parse_stream_create (demuxer, pad);
  if (!stream)
    return;

  demuxer->priv->streams = g_list_append (demuxer->priv->streams, stream);
  GST_DEBUG ("Done linking");
}

static void
uridecodebin_pad_no_more_pads (GstElement * uridecodebin,
    GstDemuxerES * demuxer)
{
  GST_INFO ("No more pads received from %s", GST_ELEMENT_NAME (uridecodebin));
}

static GstAutoplugSelectResult
uridecodebin_autoplug_select_cb (GstElement * src, GstPad * pad,
    GstCaps * caps, GstElementFactory * factory, GstDemuxerES * demuxer)
{
  GstAutoplugSelectResult ret = GST_AUTOPLUG_SELECT_TRY;
  if (gst_element_factory_list_is_type (factory,
          GST_ELEMENT_FACTORY_TYPE_DECODER)) {
    GST_DEBUG ("Expose pad if factory is decoder.");
    ret = GST_AUTOPLUG_SELECT_EXPOSE;
  }

  return ret;
}

static gboolean
autoplug_query_caps (GstElement * uridecodebin, GstPad * pad,
    GstElement * element, GstQuery * query, GstDemuxerES * demuxer)
{
  GstCaps *result = NULL;
  GstElementFactory *factory = gst_element_get_factory (element);

  if (!factory)
    goto done;

  if (gst_element_factory_list_is_type (factory,
          GST_ELEMENT_FACTORY_TYPE_PARSER)) {
    GstCaps *caps = gst_pad_query_caps (pad, NULL);
    if (!caps) {
      GST_ERROR ("Unable to retrive caps from the parser");
      goto done;
    }
    GstStructure *s = gst_caps_get_structure (caps, 0);
    if (!s)
      goto done;
    GstDemuxerVideoCodec codec_id =
        gst_parse_stream_get_vcodec_from_caps (caps);
    switch (codec_id) {
      case DEMUXER_ES_VIDEO_CODEC_H264:
        result =
            gst_caps_from_string
            ("video/x-h264,stream-format=byte-stream,alignment=au");
        break;
      case DEMUXER_ES_VIDEO_CODEC_H265:
        result =
            gst_caps_from_string
            ("video/x-h265,stream-format=byte-stream,alignment=au");
        break;
      default:
        GST_DEBUG ("Unknown codec id %d", codec_id);
    }

    gst_caps_unref (caps);
    if (result) {
      gst_query_set_caps_result (query, result);
      GST_INFO ("the caps is %" GST_PTR_FORMAT, result);
      gst_caps_unref (result);
    }
  }

done:
  return (result != NULL);
}

static gboolean
uridecodebin_autoplug_query_cb (GstElement * uridecodebin, GstPad * pad,
    GstElement * element, GstQuery * query, GstDemuxerES * demuxer)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      return autoplug_query_caps (uridecodebin, pad, element, query, demuxer);
    default:
      return FALSE;
  }
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, GstDemuxerES * demuxer)
{
  GstDemuxerESPrivate *priv = demuxer->priv;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      set_demuxer_state (demuxer, DEMUXER_ES_STATE_ERROR);;
      break;
    }
    case GST_MESSAGE_EOS:
    {
      set_demuxer_state (demuxer, DEMUXER_ES_STATE_EOS);;
      break;
    }
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s;
      const char *sname;
      s = gst_message_get_structure (message);
      sname = gst_structure_get_name (s);
      if ((GST_MESSAGE_SRC (message) == GST_OBJECT (priv->pipeline)) &&
          (!g_strcmp0 (sname, "exit"))) {
        GST_DEBUG ("`exit` message received");
        priv->bus_exit = TRUE;
      }
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
  while (check_message) {
    GstMessage *message = gst_bus_timed_pop (bus, 1);
    if (G_LIKELY (message)) {
      handle_bus_message (bus, message, demuxer);
      gst_message_unref (message);
    } else
      check_message = FALSE;
  }
  gst_object_unref (bus);

  return TRUE;
}

static gpointer
check_for_bus_message_cb (gpointer data)
{
  GstDemuxerES *demuxer = (GstDemuxerES *) data;
  GstDemuxerESPrivate *priv = demuxer->priv;
  while (!priv->bus_exit) {
    check_for_bus_message (demuxer);
  }
  return NULL;
}

GstDemuxerES *
gst_demuxer_es_new (const gchar * uri)
{
  GstDemuxerES *demuxer;
  GstDemuxerESPrivate *priv;
  GstElement *uridecodebin;
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
  g_cond_init (&priv->queue_item_del);
  priv->max_queue_size = DEMUXER_ES_DEFAULT_MAX_QUEUE_SIZE;
  priv->threshold_queue_size = DEMUXER_ES_DEFAULT_THRESHOLD_QUEUE_SIZE;

  priv->current_uri = get_gst_valid_uri (uri);

  priv->packets = g_async_queue_new_full ((GDestroyNotify) g_free);

  priv->pipeline = gst_pipeline_new ("demuxeres");

  uridecodebin = gst_element_factory_make ("uridecodebin", NULL);
  g_object_set (G_OBJECT (uridecodebin), "uri", priv->current_uri, NULL);

  g_signal_connect (uridecodebin, "pad-added",
      G_CALLBACK (uridecodebin_pad_added_cb), demuxer);
  g_signal_connect (uridecodebin, "no-more-pads",
      G_CALLBACK (uridecodebin_pad_no_more_pads), demuxer);
  g_signal_connect (uridecodebin, "autoplug-select",
      G_CALLBACK (uridecodebin_autoplug_select_cb), demuxer);
  g_signal_connect (uridecodebin, "autoplug-query",
      G_CALLBACK (uridecodebin_autoplug_query_cb), demuxer);

  gst_bin_add_many (GST_BIN (priv->pipeline), uridecodebin, NULL);

  priv->bus_thread =
      g_thread_new ("gst_bus_thread", check_for_bus_message_cb, demuxer);

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
  if (demuxer && !wait_for_demuxer_ready (demuxer)) {
    GST_ERROR ("The demuxer did not get ready state = %d", priv->state);
    gst_demuxer_es_teardown (demuxer);
    demuxer = NULL;
  }
  return demuxer;
}

void
gst_demuxer_es_clear_packet (GstDemuxerESPacket * packet)
{
  GST_LOG ("clear packet: %d", packet->packet_number);
  gst_sample_unref (packet->priv->sample);
  g_free (packet->priv);
  g_free (packet);
}

GstDemuxerESResult
gst_demuxer_es_read_packet (GstDemuxerES * demuxer,
    GstDemuxerESPacket ** packet)
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
  if (!queue_is_filled (priv, TRUE)) {
    GST_QUEUE_SIGNAL_DEL (priv);
  }
  if (queued_packet) {
    *packet = queued_packet;
    result = DEMUXER_ES_RESULT_NEW_PACKET;
    if (priv->state == DEMUXER_ES_STATE_EOS
        && g_async_queue_length (priv->packets) == 0) {
      result = DEMUXER_ES_RESULT_LAST_PACKET;
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
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-demuxeres.best_stream");

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
gst_demuxer_es_set_queue_attributes (GstDemuxerES * demuxer,
    gint max_queue_size, gint threshold_queue_size)
{
  demuxer->priv->max_queue_size = max_queue_size;
  demuxer->priv->threshold_queue_size = threshold_queue_size;
}

static void
_gst_demuxer_es_cleanup_bus_watch (GstDemuxerES * demuxer)
{
  g_assert (demuxer != NULL);
  GstDemuxerESPrivate *priv = demuxer->priv;
  GstBus *bus = gst_element_get_bus (demuxer->priv->pipeline);
  GThread *bus_thread = priv->bus_thread;
  if (G_LIKELY (bus) && priv->bus_thread) {
    gst_bus_post (bus,
        gst_message_new_element (GST_OBJECT (priv->pipeline),
            gst_structure_new_empty ("exit")));
    GST_LOG ("waiting for message bus thread");
    priv->bus_thread = NULL;
    gst_object_unref (bus);

    g_thread_join (bus_thread);
  }
  priv->bus_exit = TRUE;
}

void
gst_demuxer_es_teardown (GstDemuxerES * demuxer)
{
  GstDemuxerESPrivate *priv = demuxer->priv;
  _gst_demuxer_es_cleanup_bus_watch (demuxer);
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);

  g_async_queue_unref (priv->packets);
  g_list_free_full (priv->streams, (GDestroyNotify) gst_parse_stream_teardown);
  gst_object_unref (priv->pipeline);

  g_cond_clear (&priv->queue_item_del);
  g_cond_clear (&priv->ready_cond);
  g_mutex_clear (&priv->ready_mutex);
  g_mutex_clear (&priv->queue_mutex);
  g_free (priv->current_uri);
  g_free (priv);
  demuxer->priv = NULL;
  g_free (demuxer);
}
