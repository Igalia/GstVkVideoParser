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

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cinttypes>

#include "dump.h"
#include "videoparser.h"

#include <vk_video/vulkan_video_codecs_common.h>
#include <vulkan/vulkan_beta.h>


static VkVideoCodecOperationFlagBitsKHR codec = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;

class PictureParameterSet : public VkParserVideoRefCountBase {
public:
    static PictureParameterSet* create()
    {
        return new PictureParameterSet();
    }

    int32_t AddRef() final
    {
        return ++m_refCount;
    }

    int32_t Release() final
    {
        uint32_t ret = --m_refCount;
        // Destroy the device if refcount reaches zero
        if (ret == 0)
            delete this;
        return ret;
    }

private:
    std::atomic<int32_t> m_refCount;

    PictureParameterSet()
        : m_refCount(0)
    {
    }
};

class Picture : public VkPicIf {
public:
    Picture()
        : m_refCount(0)
    {
    }

    void AddRef() final
    {
        assert(m_refCount >= 0);
        ++m_refCount;
    }

    void Release() final
    {
        assert(m_refCount > 0);
        int32_t ref = --m_refCount;
        if (ref == 0) {
            decodeHeight = 0;
            decodeWidth = 0;
            decodeSuperResWidth = 0;
        }
    }

    bool isAvailable() const { return m_refCount == 0; }

private:
    std::atomic<int32_t> m_refCount;
};

class VideoParserClient : public VkParserVideoDecodeClient {
public:
    VideoParserClient()
        : m_dpb(32)
    {
    }

    int32_t BeginSequence(const VkParserSequenceInfo* info) final
    {
        int32_t max = 16, conf = 1;

        fprintf(stdout, "%s\n", __FUNCTION__);

        dump_parser_sequence_info(info);

        if (info->eCodec == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT)
            max++;

        if (info->nMinNumDecodeSurfaces > 0)
            conf += info->nMinNumDecodeSurfaces - (info->isSVC ? 3 : 1);
        if (conf > max)
            conf = max;

        return std::min(conf, 17);
    }

    bool AllocPictureBuffer(VkPicIf** pic) final
    {
        fprintf(stdout, "%s\n", __FUNCTION__);

        for (auto& apic : m_dpb) {
            if (apic.isAvailable()) {
                apic.AddRef();
                *pic = &apic;
                return true;
            }
        }

        return false;
    }

    bool DecodePicture(VkParserPictureData* pic) final
    {
        fprintf(stdout, "%s - %" PRIu32 "\n", __FUNCTION__, pic->nBitstreamDataLen);
        dump_parser_picture_data(codec, pic);
        return true;
    }

    bool UpdatePictureParameters(VkPictureParameters* params, VkSharedBaseObj<VkParserVideoRefCountBase>& shared, uint64_t count) final
    {
        // fprintf(stdout, "%s: %" PRIu64 "\n", __FUNCTION__, count);
        fprintf(stdout, "%s\n", __FUNCTION__);
        shared = PictureParameterSet::create();
        dump_picture_parameters(params);
        return true;
    }

    bool DisplayPicture(VkPicIf* pic, int64_t ts) final
    {
        fprintf(stdout, "%s\n", __FUNCTION__);
        return true;
    }

    void UnhandledNALU(const uint8_t*, int32_t) final
    {
        fprintf(stdout, "%s\n", __FUNCTION__);
    }

    ~VideoParserClient()
    {
        for (auto& pic : m_dpb)
            assert(pic.isAvailable());
    }

private:
    std::vector<Picture> m_dpb;
};

static bool parse(FILE* stream)
{
    VulkanVideoDecodeParser* parser = nullptr;
    VideoParserClient client = VideoParserClient();
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

    fprintf(stdout, "%s\n", __FUNCTION__);

    static const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };
    static const VkExtensionProperties h265StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION };

    const VkExtensionProperties* pStdExtensionVersion = NULL;
    if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT) {
        pStdExtensionVersion = &h264StdExtensionVersion;
    } else if (codec == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT) {
        pStdExtensionVersion = &h265StdExtensionVersion;
    } else {
        assert(!"Unsupported Codec Type");
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
            fprintf(stdout, "failed to parse bitstream.\n");
            break;
        }

        assert(pkt.nDataLength == parsed);
    }

    ret = (parser->Release() == 0);
    assert(ret);
    return ret;
}

int process_file (gchar* filename) {
    FILE* file;
    file = fopen(filename, "r");
    if (!file) {
        g_printerr( "Unable to open: %s -- %s.\n", filename, strerror(errno));
        return EXIT_FAILURE;
    }

    if (!parse(file)) {
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
    gint ret = EXIT_SUCCESS;

    static GOptionEntry entries[] = {
        { "codec", 'c', 0, G_OPTION_ARG_STRING, &codec_str, "Codec to use ie h265", NULL },
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL},
        { NULL }
    };

    g_set_prgname (argv[0]);

    ctx = g_option_context_new ("TEST");
    g_option_context_add_main_entries (ctx, entries, NULL);


    if (argc == 1) {
        g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
        exit (EXIT_FAILURE);;
    }

    if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
        g_printerr ("Error initializing: %s\n", err->message);
        g_option_context_free (ctx);
        g_clear_error (&err);
        exit (EXIT_FAILURE);
    }

    if (!(filenames != NULL && *filenames != NULL)) {
        g_printerr ("Please provide one or more filenames.");
        exit (EXIT_FAILURE);
    }

    if (codec_str && strcmp (codec_str, "h265") == 0)
      codec = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT;


    ret |= process_file (filenames[0]);

    return ret;
}
