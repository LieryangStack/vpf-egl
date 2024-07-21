#ifndef __GST_EGL_H__
#define __GST_EGL_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

G_BEGIN_DECLS

#define GST_EGL_IMAGE_MEMORY_TYPE "EGLImage"

#define GST_CAPS_FEATURE_MEMORY_EGL_IMAGE "memory:EGLImage"

typedef struct _GstEGLDisplay GstEGLDisplay;

/* EGLImage GstMemory handling */
gboolean gst_egl_image_memory_is_mappable (void);
gboolean gst_is_egl_image_memory (GstMemory * mem);
EGLImageKHR gst_egl_image_memory_get_image (GstMemory * mem);
GstEGLDisplay *gst_egl_image_memory_get_display (GstMemory * mem);
GstVideoGLTextureType gst_egl_image_memory_get_type (GstMemory * mem);
GstVideoGLTextureOrientation gst_egl_image_memory_get_orientation (GstMemory * mem);
void gst_egl_image_memory_set_orientation (GstMemory * mem,
    GstVideoGLTextureOrientation orientation);

/* 创建GstEGLImageAllocator对象函数，但是不支持内存 map、copy以及其他事情 */
GstAllocator *gst_egl_image_allocator_new (void);

GstMemory *
gst_egl_image_allocator_alloc (GstAllocator * allocator,
                               GstEGLDisplay * display, 
                               GstVideoGLTextureType type, 
                               gint width,
                               gint height, 
                               gsize * size); /* 这是个返回NULL的空函数 */

GstMemory *
gst_egl_image_allocator_wrap (GstAllocator * allocator,
                              GstEGLDisplay * display, 
                              EGLImageKHR image, 
                              GstVideoGLTextureType type,
                              GstMemoryFlags flags, 
                              gsize size, 
                              gpointer user_data,
                              GDestroyNotify user_data_destroy); /* 真正创建GstEGLImageMemory对象的函数  */


#define GST_EGL_DISPLAY_CONTEXT_TYPE "gst.egl.EGLDisplay"
GstContext * gst_context_new_egl_display (GstEGLDisplay * display, gboolean persistent);
gboolean gst_context_get_egl_display (GstContext * context,
    GstEGLDisplay ** display);

/* EGLDisplay wrapper with refcount, connection is closed after last ref is gone */
#define GST_TYPE_EGL_DISPLAY (gst_egl_display_get_type())
GType gst_egl_display_get_type (void);

GstEGLDisplay *gst_egl_display_new (EGLDisplay display, GDestroyNotify destroy_notify);
GstEGLDisplay *gst_egl_display_ref (GstEGLDisplay * display);
void gst_egl_display_unref (GstEGLDisplay * display);
EGLDisplay gst_egl_display_get (GstEGLDisplay * display);
EGLImageKHR gst_egl_display_image_create (GstEGLDisplay * display,
    EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
    const EGLint * attrib_list);

G_END_DECLS
#endif /* __GST_EGL_H__ */
