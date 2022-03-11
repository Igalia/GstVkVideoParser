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

#include <cstdlib>
#include <videoparser.h>
#include <vk_video/vulkan_video_codecs_common.h>

class VideoParserClient : public VkParserVideoDecodeClient {
public:
    int32_t BeginSequence(const VkParserSequenceInfo*) final { return 2; }
    bool AllocPictureBuffer(VkPicIf**) final { return false; }
    bool DecodePicture(VkParserPictureData*) final { return false; }
    bool UpdatePictureParameters(VkPictureParameters*, VkSharedBaseObj<VkParserVideoRefCountBase>&, uint64_t) final { return false; }
    bool DisplayPicture(VkPicIf*, int64_t) final { return false; }
    void UnhandledNALU(const uint8_t*, int32_t) final { };

    ~VideoParserClient() { }
};

static bool parse()
{
    VulkanVideoDecodeParser* parser = nullptr;
    VkParserInitDecodeParameters params = {
         .interfaceVersion = VK_MAKE_VIDEO_STD_VERSION(0, 9, 1),
         .pClient = new VideoParserClient(),
    };
    bool ret;

    ret = CreateVulkanVideoDecodeParser(&parser, VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT, nullptr, 0);
    assert(ret);
    if (!ret)
        return ret;

    ret = (parser->Initialize(&params) == VK_SUCCESS);
    assert(ret);
    if (!ret)
        return ret;

    ret = (parser->Release() == 0);
    assert(ret);
    return ret;
}

int main(int argc, char **argv)
{
    if (!parse())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
