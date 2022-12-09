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

#include "vkvideodecodeparser.h"
#include "gstvkvideoparser.h"

#include <vk_video/vulkan_video_codecs_common.h>


GST_DEBUG_CATEGORY_EXTERN (gst_vk_video_parser_debug);
#define GST_CAT_DEFAULT gst_vk_video_parser_debug

class GstVkVideoDecoderParser : public VulkanVideoDecodeParser {
public:
    GstVkVideoDecoderParser(VkVideoCodecOperationFlagBitsKHR codec)
        : m_refCount(1)
        , m_codec(codec)
        , m_parser(nullptr)
    {
    }

    VkResult Initialize(VkParserInitDecodeParameters*) final;
    bool Deinitialize() final;
    bool ParseByteStream(const VkParserBitstreamPacket*, int32_t*) final;

    // not implemented
    bool DecodePicture(VkParserPictureData*) final { return false; }
    bool DecodeSliceInfo(VkParserSliceInfo*, const VkParserPictureData*, int32_t) final { return false; }
    bool GetDisplayMasteringInfo(VkParserDisplayMasteringInfo*) final { return false; }

    int32_t AddRef() final;
    int32_t Release() final;

private:
    ~GstVkVideoDecoderParser() {}

    int m_refCount;
    VkVideoCodecOperationFlagBitsKHR m_codec;
    GstVkVideoParser* m_parser;
};

VkResult GstVkVideoDecoderParser::Initialize(VkParserInitDecodeParameters* params)
{
    if (!(params && params->interfaceVersion == NV_VULKAN_VIDEO_PARSER_API_VERSION))
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!params->pClient)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!gst_init_check(NULL, NULL, NULL))
        return VK_ERROR_INITIALIZATION_FAILED;

    m_parser = new GstVkVideoParser(params->pClient, m_codec, params->bOutOfBandPictureParameters);
    if (!m_parser->Build())
        return VK_ERROR_INITIALIZATION_FAILED;

    return VK_SUCCESS;
}

bool GstVkVideoDecoderParser::Deinitialize()
{
    if (m_parser) {
        delete m_parser;
        m_parser  = nullptr;
    }
    return true;
}

bool GstVkVideoDecoderParser::ParseByteStream(const VkParserBitstreamPacket* bspacket, int32_t* parsed)
{
    if (parsed)
        *parsed = 0;

    if (bspacket->nDataLength) {
         auto buffer = gst_buffer_new_memdup(bspacket->pByteStream, bspacket->nDataLength);
        if (!buffer)
            return false;

        auto ret = m_parser->PushBuffer(buffer);
        if (ret != GST_FLOW_OK)
            return false;
    }

    if (bspacket->bEOS) {
        auto ret = m_parser->Eos();
        if (ret != GST_FLOW_EOS)
            return false;
    }

    if (parsed)
        *parsed = bspacket->nDataLength;

    return true;
}

int32_t GstVkVideoDecoderParser::AddRef()
{
    g_atomic_int_inc(&m_refCount);
    return m_refCount;
}

int32_t GstVkVideoDecoderParser::Release()
{
    if (g_atomic_int_dec_and_test(&m_refCount)) {
        Deinitialize();
        delete this;
        return 0;
    }

    return m_refCount;
}

bool CreateVulkanVideoDecodeParser(VulkanVideoDecodeParser** parser, VkVideoCodecOperationFlagBitsKHR codec,
                                   const VkExtensionProperties* pStdExtensionVersion,
                                   nvParserLogFuncType pParserLogFunc, int logLevel = 0)
{
    if (!parser)
        return false;

    auto* internalParser = new GstVkVideoDecoderParser(codec);
    if (!internalParser)
        return false;

    *parser = internalParser;
    return true;
}
