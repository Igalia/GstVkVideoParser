#ifndef __GST_VK_ELEMENT_H__
#define __GST_VK_ELEMENT_H__

#include <gst/gst.h>


#include "gstvkh264dec.h"
#include "gstvkh265dec.h"

G_BEGIN_DECLS

void vk_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DECLARE (vkh264parse);
GST_ELEMENT_REGISTER_DECLARE (vkh265parse);

GST_DEBUG_CATEGORY_EXTERN (gst_vk_parser_debug);

G_END_DECLS

#endif /* __GST_VK_ELEMENT_H__ */
