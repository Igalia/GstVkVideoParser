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
#include <gmodule.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cinttypes>

#include "dump.h"

#include <vk_video/vulkan_video_codecs_common.h>
#include <vulkan/vulkan_beta.h>


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
    VideoParserClient(VkVideoCodecOperationFlagBitsKHR codec, bool quiet)
        : m_dpb(32),
        m_quiet(quiet),
        m_codec(codec)
    {
    }

    int32_t BeginSequence(const VkParserSequenceInfo* info) final
    {
        int32_t max = 16, conf = 1;

        fprintf(stdout, "%s\n", __FUNCTION__);
        if (!m_quiet)
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
        if (!m_quiet)
            dump_parser_picture_data(m_codec, pic);
        return true;
    }

    bool UpdatePictureParameters(VkPictureParameters* params, VkSharedBaseObj<VkParserVideoRefCountBase>& shared, uint64_t count) final
    {
        // fprintf(stdout, "%s: %" PRIu64 "\n", __FUNCTION__, count);
        fprintf(stdout, "%s\n", __FUNCTION__);
        shared = PictureParameterSet::create();
        if (!m_quiet)
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
    bool m_quiet;
    VkVideoCodecOperationFlagBitsKHR m_codec;

};

