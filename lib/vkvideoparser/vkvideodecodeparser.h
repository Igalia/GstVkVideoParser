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

#define VK_ENABLE_BETA_EXTENSIONS 1
#include <vulkan/vulkan.h>
#include <VulkanVideoParserIf.h>


typedef void (*nvParserLogFuncType)(const char* format, ...);

#ifdef __cplusplus
extern "C" {
#endif

bool CreateVulkanVideoDecodeParser(VulkanVideoDecodeParser** ppobj, VkVideoCodecOperationFlagBitsKHR eCompression,
                                   const VkExtensionProperties* pStdExtensionVersion,
                                   nvParserLogFuncType pParserLogFunc, int logLevel);
#ifdef __cplusplus
} // extern "C"
#endif
