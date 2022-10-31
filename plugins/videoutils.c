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

#include "videoutils.h"

uint32_t
pack_framerate (uint32_t numerator, uint32_t denominator)
{
  while ((numerator >= (1 << 18)) || (denominator >= (1 << 14))) {
    if (!(numerator % 5) && !(denominator % 5)) {
      numerator /= 5;
      denominator /= 5;
    } else if (((numerator | denominator) & 1) && !(numerator % 3)
        && !(denominator % 3)) {
      numerator /= 3;
      denominator /= 3;
    } else {
      numerator = (numerator + 1) >> 1;
      denominator = (denominator + 1) >> 1;
    }
  }
  return MAKEFRAMERATE (numerator, denominator);
}
