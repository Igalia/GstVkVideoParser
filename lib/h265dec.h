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
#include "gsth265decoder.h"

G_BEGIN_DECLS

#define GST_TYPE_H265_DEC gst_h265_dec_get_type()

G_DECLARE_FINAL_TYPE (GstH265Dec, gst_h265_dec, GST, H265_DEC, GstH265Decoder)

G_END_DECLS
