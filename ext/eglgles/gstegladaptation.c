
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


GstCaps *
gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext *
    ctx)
{
  GstCaps *caps = NULL, *copy1, *copy2;
  guint i, n;

  GST_DEBUG_OBJECT (ctx->element,
      "Building initial list of wanted eglattribs per format");

  caps = gst_caps_new_empty ();

  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16));

  copy1 = gst_caps_copy (caps);
  copy2 = gst_caps_copy (caps);

  #ifndef HAVE_IOS
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) { /* video/x-raw(memory:EGLImage) */
    GstCapsFeatures *features =
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE, NULL);
    gst_caps_set_features (caps, i, features);
  }
  #endif

  n = gst_caps_get_size (copy1);
  for (i = 0; i < n; i++) { /* video/x-raw(meta:GstVideoGLTextureUploadMeta) */
    GstCapsFeatures *features =
        gst_caps_features_new
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, NULL);
    gst_caps_set_features (copy1, i, features);
  }

  gst_caps_append (caps, copy1);
  gst_caps_append (caps, copy2);

  n = gst_caps_get_size (caps);
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
  for (i = n; i < n+6; i++) {  /* video/x-raw(memory:NVMM) */
    GstCapsFeatures *features =
        gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, i, features);
  }

  /* 是否应该添加检查  是否支持 RGBA8888 config 属性？？ */
  // } else {
  //   GST_INFO_OBJECT (ctx->element,
  //       "EGL display doesn't support RGBA8888 config");
  // }

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
 * 选择egl配置组时候所需的属性信息
*/
static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};


/**
 * @brief: 获取Buffer，但是该该Buffer中的GstMemory的图片是EGLImageKHR，
 *         这个EGLImageKHR是空的，因为里面函数新生成一个纹理（该纹理并没有被更新）
 * @calledby: 只会在查询元素的时候 eglglessink-allocate-eglimage 时候，才会调用，调用几率很小
*/
GstBuffer *
gst_egl_image_allocator_alloc_eglimage (GstAllocator * allocator,
    GstEGLDisplay * display, EGLContext eglcontext, GstVideoFormat format,
    gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  guint i;
  gint stride[GST_VIDEO_MAX_PLANES]; /* 一行占用的多少字节内存，比如RGB888就是 3 * width */
  gsize offset[GST_VIDEO_MAX_PLANES];
  GstMemory *mem[3] = { NULL, NULL, NULL }; /* 新建GstBuffer，然后把该mem添加到该Buffer中，然后返回 */
  guint n_mem;
  GstMemoryFlags flags = 0;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  /* 表示该内存块不能映射到用户可访问的指针地址空间（因为可能返回的，通过当前上下文创建的EGLImageKHR） */
  if (!gst_egl_image_memory_is_mappable ())
    flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  /* See https://bugzilla.gnome.org/show_bug.cgi?id=695203 */
  flags |= GST_MEMORY_FLAG_NO_SHARE;


  gst_video_info_set_format (&info, format, width, height);

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:{
      gsize size;  /* 一帧占用的多少字节内存 */
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {  /* gst_egl_image_allocator_alloc这返回的是NULL */
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else { /* 所以会执行该部分 */
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 3);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      EGLImageKHR image;
      gsize size;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 2);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
            NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      EGLImageKHR image;
      gsize size[2];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_COMP_WIDTH (&info, 1),
          GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);

      if (mem[0] && mem[1]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 2;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        mem[0] = mem[1] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1) * 2);
        offset[1] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[0] = offset[1];
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);

        for (i = 0; i < 2; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);
          data->display = gst_egl_display_get (display);
          data->eglcontext = eglcontext;

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          if (i == 0)
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
                GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              gst_egl_display_image_create (display,
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (uintptr_t) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image,
              (i ==
                  0 ? GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE :
                  GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA),
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 2;
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      EGLImageKHR image;
      gsize size[3];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              1), GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);
      mem[2] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              2), GST_VIDEO_INFO_COMP_HEIGHT (&info, 2), &size[2]);

      if (mem[0] && mem[1] && mem[2]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[2] = size[1];
        stride[2] = size[2] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 3;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[2], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        if (mem[2])
          gst_memory_unref (mem[2]);
        mem[0] = mem[1] = mem[2] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1));
        stride[2] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 2));
        size[0] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);
        size[2] = stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 2);
        offset[0] = 0;
        offset[1] = size[0];
        offset[2] = offset[1] + size[1];

        for (i = 0; i < 3; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);
          data->display = gst_egl_display_get (display);
          data->eglcontext = eglcontext;

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
              GST_VIDEO_INFO_COMP_WIDTH (&info, i),
              GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
              GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              gst_egl_display_image_create (display,
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (uintptr_t) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image, GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE,
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 3;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        /* 通过 egl当前的上下文创建了一个 EGLImageKHR， */
        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGBA,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);

        n_mem = 1;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, 0, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;

mem_error:
  {
    GST_ERROR_OBJECT (GST_CAT_DEFAULT, "Failed to create EGLImage");

    if (data)
      gst_egl_gles_image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);
    if (mem[1])
      gst_memory_unref (mem[1]);
    if (mem[2])
      gst_memory_unref (mem[2]);

    return NULL;
  }
}

