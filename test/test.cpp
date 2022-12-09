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


#include "VideoParserClient.h"
#include "vkvideodecodeparser.h"

#ifdef _DEBUG
#   define DBG(FMT, ...) g_print(FMT, ##__VA_ARGS__)
#else
#   define DBG(FMT, ...)
#endif
#define ERR(FMT, ...) g_print("ERROR: " FMT, ##__VA_ARGS__)

static VkVideoCodecOperationFlagBitsKHR codec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;


static bool parse(FILE* stream, bool quiet)
{
    VulkanVideoDecodeParser* parser = nullptr;
    VideoParserClient client = VideoParserClient(codec, quiet);
    VkParserInitDecodeParameters params = {
        .interfaceVersion = NV_VULKAN_VIDEO_PARSER_API_VERSION,
        .pClient = &client,
        .bOutOfBandPictureParameters = true,
    };
    bool ret;
    unsigned char buf[BUFSIZ + 1];
    size_t read;
    int32_t parsed;
    VkParserBitstreamPacket pkt;

    static const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };
    static const VkExtensionProperties h265StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION };

    const VkExtensionProperties* pStdExtensionVersion = NULL;
    if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT) {
        pStdExtensionVersion = &h264StdExtensionVersion;
    } else if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT) {
        pStdExtensionVersion = &h265StdExtensionVersion;
    } else {
        ERR ("Unsupported Codec Type");
        return false;
    }

    ret = CreateVulkanVideoDecodeParser(&parser, codec, pStdExtensionVersion, (nvParserLogFuncType)printf, 50);
    assert(ret);
    if (!ret)
        return ret;

    ret = (parser->Initialize(&params) == VK_SUCCESS);

    if (!ret)
        return ret;

    while (true) {
        read = fread(buf, 1, BUFSIZ, stream);
        if (read <= 0)
            break;
        pkt = VkParserBitstreamPacket {
            .pByteStream = buf,
            .nDataLength = static_cast<int32_t>(read),
            .bEOS = read < BUFSIZ,
        };

        if (!parser->ParseByteStream(&pkt, &parsed)) {
            ERR ("failed to parse bitstream.\n");
            break;
        }

        assert(pkt.nDataLength == parsed);
    }

    ret = (parser->Deinitialize() == 0);
    ret = (parser->Release() == 0);
    assert(ret);
    return ret;
}

int process_file (gchar* filename, bool quiet) {
    FILE* file;
    DBG ("Processing file %s.\n", filename);
    file = fopen(filename, "r");
    if (!file) {
        ERR ( "Unable to open: %s -- %s.\n", filename, strerror(errno));
        return EXIT_FAILURE;
    }

    if (!parse(file, quiet)) {
        fclose(file);
        return EXIT_FAILURE;
    }

    fclose(file);
    return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    GOptionContext *ctx;
    GError *err = NULL;
    gchar **filenames = NULL;
    gchar *codec_str = NULL;
    gboolean quiet = FALSE;
    gint ret = EXIT_SUCCESS;

    static GOptionEntry entries[] = {
        { "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "Quiet parser", NULL },
        { "codec", 'c', 0, G_OPTION_ARG_STRING, &codec_str, "Codec to use ie h265", NULL },
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
        ERR ("Error initializing: %s\n", err->message);
        g_option_context_free (ctx);
        g_clear_error (&err);
        exit (EXIT_FAILURE);
    }

    g_option_context_free (ctx);

    if (!(filenames != NULL && *filenames != NULL)) {
        ERR ("Please provide one or more filenames.");
        exit (EXIT_FAILURE);
    }

    if (codec_str && strcmp (codec_str, "h265") == 0)
      codec = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT;
    g_free (codec_str);

    int num = g_strv_length (filenames);
    for (int i = 0; i < num; ++i)
        ret |= process_file (filenames[0], quiet);

     g_strfreev (filenames);

    return ret;
}
