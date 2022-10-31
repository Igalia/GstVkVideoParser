
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstvkelements.h"

GST_DEBUG_CATEGORY (gst_vk_parser_debug);

void
vk_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;
  if (g_once_init_enter (&res)) {
    GST_DEBUG_CATEGORY_INIT (gst_vk_parser_debug, "vkvideoparser", 0, "Vulkan Video Parser");
    g_once_init_leave (&res, TRUE);
  }
}
