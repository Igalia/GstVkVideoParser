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

void
print_video_info (GstDemuxerEStream * stream)
{
  g_print ("video info: \n");
  g_print ("\tcodec: %d\n", stream->data.video.vcodec);
  g_print ("\tprofile: %s\n", stream->data.video.profile);
  g_print ("\tlevel: %s\n", stream->data.video.level);
  g_print ("\twidth: %d\n", stream->data.video.info.width);
  g_print ("\theight: %d\n", stream->data.video.info.height);
  g_print ("\tbitrate: %d\n", stream->data.video.bitrate);
  g_print ("\tfps: %d/%d\n", stream->data.video.info.fps_d,
      stream->data.video.info.fps_d);
  g_print ("\tpar: %d/%d\n", stream->data.video.info.par_n,
      stream->data.video.info.par_d);
  g_print ("\n");
}

int
process_file (gchar * filename)
{
  GstDemuxerESPacket *pkt;
  GstDemuxerEStream *stream;
  GstDemuxerESResult result;
  GstDemuxerES *parser = gst_demuxer_es_new (filename);

  if (!parser) {
    g_print ("An error occured during the parser creation.\n");
    return EXIT_FAILURE;
  }

  stream =
      gst_demuxer_es_find_best_stream (parser, DEMUXER_ES_STREAM_TYPE_VIDEO);
  if (!stream) {
    g_print ("Unable to retrieve the video stream.\n");
    return EXIT_FAILURE;
  }

  print_video_info (stream);

  while ((result =
    gst_demuxer_es_read_packet (parser,
               &pkt)) <= DEMUXER_ES_RESULT_LAST_PACKET) {
    if (result <= DEMUXER_ES_RESULT_LAST_PACKET) {
       g_print ("A %s packet of type %d stream_id %d with size %lu.",
       (result == DEMUXER_ES_RESULT_LAST_PACKET)? "last":"new",
           pkt->stream_type, pkt->stream_id, pkt->data_size);
       gst_demuxer_es_clear_packet (pkt);
    } else {
      g_print ("No packet available.\n");
    }
  }

  if (result == DEMUXER_ES_RESULT_ERROR) {
    g_print ("An error occured during the read of frame.\n");
    return EXIT_FAILURE;
  } else if (result == DEMUXER_ES_RESULT_EOS)
    g_print ("The parser exited with success.\n");

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
    g_printerr ("Error initializing: %s\n", err->message);
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (EXIT_FAILURE);
  }
  g_option_context_free (ctx);
  if (!filenames) {
    g_printerr ("Please provide one or more filenames.\n");
    exit (EXIT_FAILURE);
  }

  num = g_strv_length (filenames);
  for (i = 0; i < num; ++i) {
    ret |= process_file (filenames[i]);
  }

  g_strfreev (filenames);
  return ret;
}
