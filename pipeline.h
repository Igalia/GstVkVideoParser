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
#define VK_ENABLE_BETA_EXTENSIONS 1
#include <vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_PARSER gst_video_parser_get_type()

G_DECLARE_FINAL_TYPE (GstVideoParser, gst_video_parser, GST, VIDEO_PARSER, GstObject)

GstVideoParser *         gst_video_parser_new         (gpointer user_data,
                                                       VkVideoCodecOperationFlagBitsKHR codec,
                                                       gboolean oob_pic_params);

GstFlowReturn            gst_video_parser_push_buffer (GstVideoParser *self,
						       GstBuffer *buffer);

GstFlowReturn            gst_video_parser_eos         (GstVideoParser *self);

G_END_DECLS
