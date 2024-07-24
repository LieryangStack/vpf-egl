
#include "gstegladaptation.h"
#include "gsteglglessink.h"
#include <gst/video/video.h>
#include <string.h>

#define GST_CAT_DEFAULT egladaption_debug
GST_DEBUG_CATEGORY (egladaption_debug);

void
gst_egl_adaption_init (void)
{
  GST_DEBUG_CATEGORY_INIT (egladaption_debug, "egladaption", 0,
      "EGL adaption layer");
}

/*获取EGL错误*/
gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}


/**
 * @brief: 该函数只有在创建GstBuffer的时候才会调用，而且创建的这个空GstBuffer只是为了回复别人查询，并不是真正的GstBuffer
*/
static void
gst_egl_gles_image_data_free (GstEGLGLESImageData * data)
{
  if (!eglMakeCurrent (data->display,
      EGL_NO_SURFACE, EGL_NO_SURFACE, data->eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      g_slice_free (GstEGLGLESImageData, data);
      return;
  }
  glDeleteTextures (1, &data->texture);
  g_slice_free (GstEGLGLESImageData, data);
}


/**
 * @brief: 创建一个"video/x-raw"的GstCap
*/
static GstCaps *
_gst_video_format_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

/**
 * @brief: 元素的sinkpad支持的Caps
 */
GstCaps *
gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext *ctx) {

  GstCaps *caps = NULL;
  guint i, n;

  GST_DEBUG_OBJECT (ctx->element,
      "Building initial list of wanted eglattribs per format");

  caps = gst_caps_new_empty ();

  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));

  GstCapsFeatures *features =
      gst_caps_features_new ("memory:NVMM", NULL); /* video/x-raw(memory:NVMM) */
  gst_caps_set_features (caps, 0, features);

  return caps;
}


gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    g_print ("GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief: 给GstEglAdaptationContext结构体申请内存
*/
GstEglAdaptationContext *
gst_egl_adaptation_context_new (GstElement * element)
{
  GstEglAdaptationContext *ctx = g_new0 (GstEglAdaptationContext, 1);

  ctx->element = gst_object_ref (element);

  return ctx;
}

/**
 * @brief: 释放 GstEglAdaptationContext结构体内存
*/
void
gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx)
{
  if (GST_OBJECT_REFCOUNT(ctx->element))
    gst_object_unref (ctx->element);
  g_free (ctx);
}

