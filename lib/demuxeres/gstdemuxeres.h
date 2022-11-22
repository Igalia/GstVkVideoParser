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

#pragma once

#include <glib.h>

#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DEMUXERES
    #define GST_DEMUXER_ES_API __declspec(dllexport)
  #else
    #define GST_DEMUXER_ES_API __declspec(dllimport)
  #endif
#else
  #ifdef BUILDING_DEMUXERES
      #define GST_DEMUXER_ES_API __attribute__ ((visibility ("default")))
  #else
      #define GST_DEMUXER_ES_API
  #endif
#endif

typedef struct _GstDemuxerES GstDemuxerES;
typedef struct _GstDemuxerESPrivate GstDemuxerESPrivate;
typedef struct _GstDemuxerESPacketPrivate GstDemuxerESPacketPrivate;

struct _GstDemuxerES
{
  /*< private > */
  GstDemuxerESPrivate *priv;
};

#ifdef __cplusplus
extern "C" {
#endif
typedef enum _GstDemuxerEStreamType
{
  /*< public >*/
  DEMUXER_ES_STREAM_TYPE_UNKNOWN,
  DEMUXER_ES_STREAM_TYPE_VIDEO,
  DEMUXER_ES_STREAM_TYPE_AUDIO,
  DEMUXER_ES_STREAM_TYPE_TEXT,
  DEMUXER_ES_STREAM_TYPE_DATA,
  /*< private >*/
  DEMUXER_ES_STREAM_TYPES,
} GstDemuxerEStreamType;

typedef enum GstDemuxerVideoCodec {
  DEMUXER_ES_VIDEO_CODEC_UNKNOWN,
  DEMUXER_ES_VIDEO_CODEC_H264,
  DEMUXER_ES_VIDEO_CODEC_H265,
} GstDemuxerVideoCodec;

typedef enum GstDemuxerAudioCodec {
  DEMUXER_ES_AUDIO_CODEC_UNKNOWN,
  DEMUXER_ES_AUDIO_CODEC_AAC,
} GstDemuxerAudioCodec;

typedef enum _GstDemuxerESResult
{
  /*< public >*/
  DEMUXER_ES_RESULT_NEW_PACKET = 0,
  DEMUXER_ES_RESULT_LAST_PACKET,
  DEMUXER_ES_RESULT_NO_PACKET ,
  DEMUXER_ES_RESULT_EOS,
  DEMUXER_ES_RESULT_ERROR,
} GstDemuxerESResult;

typedef struct {
    GstDemuxerESPacketPrivate * priv;
    guint8 * data;
    gsize data_size;
    GstDemuxerEStreamType stream_type;
    guint stream_id;
    guint packet_number;
    gint64 pts;
    gint64 dts;
    gint64 duration;
} GstDemuxerESPacket;

typedef struct _GstDemuxerVideoInfo {
  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  gint par_n;
  gint par_d;
  gint bitrate;
  gchar* profile;
  gchar* level;
  GstDemuxerVideoCodec vcodec;
} GstDemuxerVideoInfo;

typedef struct _GstDemuxerAudioInfo {
  gint channels;
  gint rate;
  gint width;
  gint depth;
  gint bitrate;
  GstDemuxerAudioCodec acodec;
} GstDemuxerAudioInfo;


typedef union _GstDemuxerInfoData
{
  GstDemuxerVideoInfo video;
  GstDemuxerAudioInfo audio;
} GstDemuxerInfoData;

typedef struct
{
  GstDemuxerES *demuxer;
  GstDemuxerEStreamType type;
  guint id;
  GstDemuxerInfoData info;
} GstDemuxerEStream;

GST_DEMUXER_ES_API
GstDemuxerES * gst_demuxer_es_new (const gchar * uri);

GST_DEMUXER_ES_API
GstDemuxerESResult gst_demuxer_es_read_packet (GstDemuxerES * demuxer, GstDemuxerESPacket ** packet);

GST_DEMUXER_ES_API
void gst_demuxer_es_clear_packet (GstDemuxerESPacket * packet);

GST_DEMUXER_ES_API
GstDemuxerEStream * gst_demuxer_es_find_best_stream (GstDemuxerES * demuxer, GstDemuxerEStreamType type);

GST_DEMUXER_ES_API
void gst_demuxer_es_teardown (GstDemuxerES * demuxer);

#ifdef __cplusplus
}
#endif