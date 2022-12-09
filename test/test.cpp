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

#include <glib.h>

#include "gstdemuxeres.h"
#include "utils.h"
#include "VideoParserClient.h"
#include "vkvideodecodeparser.h"

static gboolean parse(gchar* filename, bool quiet)
{
    VulkanVideoDecodeParser* parser = nullptr;

    bool ret;
    int32_t parsed;
    VkParserBitstreamPacket pkt;
    GstDemuxerESPacket * demuxer_pkt;
    GstDemuxerEStream * demuxer_video_stream;
    GstDemuxerESResult result;
    VkVideoCodecOperationFlagBitsKHR codec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;
    GstDemuxerES *demuxer = gst_demuxer_es_new (filename);

    if (!demuxer) {
        ERR ("Unable to create the demuxer.");
        return false;
    }

    demuxer_video_stream =
      gst_demuxer_es_find_best_stream (demuxer, DEMUXER_ES_STREAM_TYPE_VIDEO);
    if (!demuxer_video_stream) {
        ERR ("Unable to retrieve the video stream.");
        return false;
    }

    static const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };
    static const VkExtensionProperties h265StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION };

    const VkExtensionProperties* pStdExtensionVersion = NULL;
    if (demuxer_video_stream->data.video.vcodec == DEMUXER_ES_VIDEO_CODEC_H264) {
        codec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;
        pStdExtensionVersion = &h264StdExtensionVersion;
    } else if (demuxer_video_stream->data.video.vcodec == DEMUXER_ES_VIDEO_CODEC_H265) {
        codec = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT;
        pStdExtensionVersion = &h265StdExtensionVersion;
    } else {
        ERR ("Unsupported Codec Type");
        return false;
    }

    VideoParserClient client = VideoParserClient(codec, quiet);
    VkParserInitDecodeParameters params = {
        .interfaceVersion = NV_VULKAN_VIDEO_PARSER_API_VERSION,
        .pClient = &client,
        .bOutOfBandPictureParameters = true,
    };

    ret = CreateVulkanVideoDecodeParser(&parser, codec, pStdExtensionVersion, (nvParserLogFuncType)printf, 50);
    assert(ret);
    if (!ret)
        return ret;

    ret = (parser->Initialize(&params) == VK_SUCCESS);

    if (!ret)
        return ret;

    while ((result =
            gst_demuxer_es_read_packet (demuxer,
                &demuxer_pkt)) <= DEMUXER_ES_RESULT_NO_PACKET) {
        if (result <= DEMUXER_ES_RESULT_EOS) {
            if (result <= DEMUXER_ES_RESULT_LAST_PACKET) {
                pkt = VkParserBitstreamPacket {
                .pByteStream = demuxer_pkt->data,
                .nDataLength = static_cast<int32_t>(demuxer_pkt->data_size),
                .bEOS = (result == DEMUXER_ES_RESULT_LAST_PACKET),
                };
                DBG ("A %s packet of type %d stream_id %d with size %i.",
                pkt.bEOS ? "last":"new",
                demuxer_pkt->stream_type, demuxer_pkt->stream_id, pkt.nDataLength);
                if (!parser->ParseByteStream(&pkt, &parsed)) {
                   ERR ("failed to parse bitstream.");
                   result = DEMUXER_ES_RESULT_ERROR;
                }
            } else {
                DBG ("No packet available. Continue ...");
                continue;
            }
            gst_demuxer_es_clear_packet (demuxer_pkt);
        }
    }

    ret = (parser->Deinitialize() == 0);
    ret = (parser->Release() == 0);
    assert(ret);
    gst_demuxer_es_teardown (demuxer);
    if (result != DEMUXER_ES_RESULT_EOS)
        ERR ("The decode test ended with status %d", result);
    return (result == DEMUXER_ES_RESULT_EOS);
}


int main(int argc, char** argv)
{
    GOptionContext *ctx;
    GError *err = NULL;
    gchar **filenames = NULL;
    gboolean quiet = FALSE;
    gboolean ret = true;

    static GOptionEntry entries[] = {
        { "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "Quiet parser", NULL },
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL},
        { NULL }
    };

    g_set_prgname (argv[0]);

    ctx = g_option_context_new ("TEST");
    g_option_context_add_main_entries (ctx, entries, NULL);


    if (argc == 1) {
        ERR ("%s", g_option_context_get_help (ctx, FALSE, NULL));
        exit (EXIT_FAILURE);;
    }

    if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
        ERR ("Error initializing: %s", err->message);
        g_option_context_free (ctx);
        g_clear_error (&err);
        exit (EXIT_FAILURE);
    }

    g_option_context_free (ctx);

    if (!(filenames != NULL && *filenames != NULL)) {
        ERR ("Please provide one or more filenames.");
        exit (EXIT_FAILURE);
    }

    int num = g_strv_length (filenames);
    for (int i = 0; i < num; ++i) {
        ret &= parse (filenames[i], quiet);
    }
     g_strfreev (filenames);
    
    return ret ? EXIT_SUCCESS: EXIT_FAILURE;
}
