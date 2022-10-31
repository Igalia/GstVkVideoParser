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

#pragma once

#include <gst/gst.h>
#include <gst/check/gstharness.h>
#define VK_ENABLE_BETA_EXTENSIONS 1
#include <vulkan/vulkan.h>

G_BEGIN_DECLS

class GstVkVideoParser {
public:
    GstVkVideoParser(gpointer user_data,
                                       VkVideoCodecOperationFlagBitsKHR codec,
                                       gboolean oob_pic_params);
    ~GstVkVideoParser();

    bool Build();
    GstFlowReturn PushBuffer(GstBuffer *buffer);
    void ProcessMessages ();
    GstFlowReturn Eos();

private:
    void* m_user_data;
    VkVideoCodecOperationFlagBitsKHR m_codec;
    bool m_oob_pic_params;
    GstHarness* m_parser;
    GstBus* m_bus;
};

G_END_DECLS
