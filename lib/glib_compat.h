/* glib_compat.h
 * Copyright (C) 2022 Igalia, S.L.
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

#include <glib.h>
#include <stdint.h>

#ifndef GLIB_COMPAT_H
#define GLIB_COMPAT_H

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION (2,64,0)

typedef struct _GRealArray  GRealArray;

/**
 * GArray:
 * @data: a pointer to the element data. The data may be moved as
 *     elements are added to the #GArray.
 * @len: the number of elements in the #GArray not including the
 *     possible terminating zero element.
 *
 * Contains the public fields of a GArray.
 */
struct _GRealArray
{
  guint8 *data;
  guint   len;
  guint   elt_capacity;
  guint   elt_size;
  guint   zero_terminated : 1;
  guint   clear : 1;
  gatomicrefcount ref_count;
  GDestroyNotify clear_func;
};

static inline gpointer
g_array_steal (GArray *array,
               gsize *len)
{
  GRealArray *rarray;
  gpointer segment;

  g_return_val_if_fail (array != NULL, NULL);

  rarray = (GRealArray *) array;
  segment = (gpointer) rarray->data;

  if (len != NULL)
    *len = rarray->len;

  rarray->data  = NULL;
  rarray->len   = 0;
  rarray->elt_capacity = 0;
  return segment;
}

static inline guint8 *
g_byte_array_steal (GByteArray *array,
                    gsize *len)
{
  return (guint8 *) g_array_steal ((GArray *) array, len);
}

#endif
#endif
G_END_DECLS
