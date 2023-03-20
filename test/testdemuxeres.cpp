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

#include <gst/gst.h>
#include "utils.h"
#include "gstdemuxeres.h"
#include "stdlib.h"

void
print_video_info (GstDemuxerEStream * stream)
{
  INFO ("video info: ");
  INFO ("\tcodec: %d", stream->data.video.vcodec);
  INFO ("\tprofile: %s", stream->data.video.profile);
  INFO ("\tlevel: %s", stream->data.video.level);
  INFO ("\twidth: %d", stream->data.video.info.width);
  INFO ("\theight: %d", stream->data.video.info.height);
  INFO ("\tbitrate: %d", stream->data.video.bitrate);
  INFO ("\tfps: %d/%d", stream->data.video.info.fps_n,
      stream->data.video.info.fps_d);
  INFO ("\tpar: %d/%d", stream->data.video.info.par_n,
      stream->data.video.info.par_d);
  INFO ("");
}

int
process_file (gchar * filename)
{
  GstDemuxerESPacket *pkt;
  GstDemuxerEStream *stream;
  GstDemuxerESResult result;
  GstDemuxerES *demuxer = gst_demuxer_es_new (filename);
  gint count = 0;

  if (!demuxer) {
    ERR ("An error occured during the parser creation.");
    return EXIT_FAILURE;
  }

  stream =
      gst_demuxer_es_find_best_stream (demuxer, DEMUXER_ES_STREAM_TYPE_VIDEO);
  if (!stream) {
    ERR ("Unable to retrieve the video stream.");
    return EXIT_FAILURE;
  }

  print_video_info (stream);

  while ((result =
          gst_demuxer_es_read_packet (demuxer,
              &pkt)) <= DEMUXER_ES_RESULT_NO_PACKET) {
    if (result <= DEMUXER_ES_RESULT_LAST_PACKET) {
      INFO ("A %s packet of type %d stream_id %d with size %lu.",
      (result == DEMUXER_ES_RESULT_LAST_PACKET)? "last":"new",
          pkt->stream_type, pkt->stream_id, pkt->data_size);
      count++;
      gst_demuxer_es_clear_packet (pkt);
      if(result == DEMUXER_ES_RESULT_LAST_PACKET)
        break;
    } else {
      ERR ("No packet available.");
    }
  }

  if (result == DEMUXER_ES_RESULT_ERROR) {
    ERR ("An error occured during the read of frame.");
    return EXIT_FAILURE;
  } else if (result == DEMUXER_ES_RESULT_LAST_PACKET)
    DBG ("The parser exited with success. Found %d packet(s).", count);

  gst_demuxer_es_teardown (demuxer);
  return EXIT_SUCCESS;
}

int
main (int argc, char **argv)
{
  GOptionContext *ctx;
  GError *err = NULL;
  gchar **filenames = NULL;
  int num, i;

  gint ret = EXIT_SUCCESS;

  const GOptionEntry entries[] = {
    {G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY,
        &filenames, "Media files to play", NULL},
    {NULL,},
  };

  g_set_prgname (argv[0]);

  ctx = g_option_context_new ("TEST");
  g_option_context_add_main_entries (ctx, entries, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    ERR ("Error initializing: %s", err->message);
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (EXIT_FAILURE);
  }
  g_option_context_free (ctx);
  if (!filenames) {
    ERR ("Please provide one or more filenames.");
    exit (EXIT_FAILURE);
  }

  num = g_strv_length (filenames);
  for (i = 0; i < num; ++i) {
    ret |= process_file (filenames[i]);
  }

  g_strfreev (filenames);
  return ret;
}
