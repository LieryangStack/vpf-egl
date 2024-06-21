/***
 * @brief: 
 *         1. x显示和窗口的创建后销毁
 *         2. egl所有相关函数，egl显示、配置、表面、上下文等的创建和销毁
 * 
*/

#include "gstegladaptation.h"
#include "gsteglglessink.h"
#include "video_platform_wrapper.h"
#include <string.h>

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gst/egl/egl.h>

#define GST_CAT_DEFAULT egladaption_debug

/* Some EGL implementations are reporting wrong
 * values for the display's EGL_PIXEL_ASPECT_RATIO.
 * They are required by the khronos specs to report
 * this value as w/h * EGL_DISPLAY_SCALING (Which is
 * a constant with value 10000) but at least the
 * Galaxy SIII (Android) is reporting just 1 when
 * w = h. We use these two to bound returned values to
 * sanity.
 */
#define EGL_SANE_DAR_MIN ((EGL_DISPLAY_SCALING)/10)
#define EGL_SANE_DAR_MAX ((EGL_DISPLAY_SCALING)*10)

#define GST_EGLGLESSINK_EGL_MIN_VERSION 1

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

/*
 * GstEglGlesRenderContext:
 * @config: Current EGL config
 * @eglcontext: Current EGL context
 * @egl_minor: EGL version (minor)
 * @egl_major: EGL version (major)
 *
 * This struct holds the sink's EGL/GLES rendering context.
 */
struct _GstEglGlesRenderContext
{
  EGLConfig config;
  EGLContext eglcontext;
  EGLSurface surface;
  EGLint egl_minor, egl_major;
};

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
 * 打印可用的 EGL/GLES 扩展
 * 如果你用另外一种方式进行渲染，比如EGLImageKHR，这时你需要检查EGL/GLES是否支持该扩展可用。
*/
void
gst_egl_adaptation_init_exts (GstEglAdaptationContext * ctx)
{
#ifndef GST_DISABLE_GST_DEBUG
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (gst_egl_display_get (ctx->display), EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (ctx->element, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (ctx->element, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));
#endif
  return;
}

/**
 * @brief: eglInitialize 初始化
*/
gboolean
gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx, gchar* winsys)
{
  GstMessage *msg;
  GstEglGlesSink *sink = (GstEglGlesSink *) ctx->element;
  EGLDisplay display = EGL_NO_DISPLAY;
  GST_DEBUG_OBJECT (ctx->element, "Enter EGL initial configuration");
  
  if (!platform_wrapper_init ()) {
    GST_ERROR_OBJECT (ctx->element, "Couldn't init EGL platform wrapper");
    goto HANDLE_ERROR;
  }

  msg =
      gst_message_new_need_context (GST_OBJECT_CAST (ctx->element),
      GST_EGL_DISPLAY_CONTEXT_TYPE);
  gst_element_post_message (GST_ELEMENT_CAST (ctx->element), msg);

  GST_OBJECT_LOCK (ctx->element);
  if (!ctx->set_display) {
    GstContext *context;

    GST_OBJECT_UNLOCK (ctx->element);

    

#ifdef USE_EGL_WAYLAND
    if (g_strcmp0(winsys, "wayland") == 0) {
      display = eglGetDisplay (platform_initialize_display_wayland());
    }
#endif

#ifdef USE_EGL_X11
    if (g_strcmp0(winsys, "x11") == 0) {
      // display = eglGetDisplay (sink->display);
      display = sink->egl_display;
    }
#endif



    if (display == EGL_NO_DISPLAY) {
      GST_ERROR_OBJECT (ctx->element, "Could not get EGL display connection");
      goto HANDLE_ERROR;        /* No EGL error is set by eglGetDisplay() */
    }

    /* EGLDisplay 包装了一下， ctx->display = display; */
    ctx->display = gst_egl_display_new (display, NULL);

    context = gst_context_new_egl_display (ctx->display, FALSE);
    msg = gst_message_new_have_context (GST_OBJECT (ctx->element), context);
    gst_element_post_message (GST_ELEMENT_CAST (ctx->element), msg);
  } else {
    ctx->display = ctx->set_display;
    GST_OBJECT_UNLOCK (ctx->element);
  }

  /* 来自UI的 egl_display 已经被初始化了 */

  // if (!eglInitialize (gst_egl_display_get (ctx->display),
  //         &ctx->eglglesctx->egl_major, &ctx->eglglesctx->egl_minor)) {
  //   got_egl_error ("eglInitialize");
  //   GST_ERROR_OBJECT (ctx->element, "Could not init EGL display connection");
  //   goto HANDLE_EGL_ERROR;
  // }

  // /* Check against required EGL version
  //  * XXX: Need to review the version requirement in terms of the needed API
  //  */
  // if (ctx->eglglesctx->egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
  //   GST_ERROR_OBJECT (ctx->element, "EGL v%d needed, but you only have v%d.%d",
  //       GST_EGLGLESSINK_EGL_MIN_VERSION, ctx->eglglesctx->egl_major,
  //       ctx->eglglesctx->egl_minor);
  //   goto HANDLE_ERROR;
  // }

  ctx->eglglesctx->egl_major = 3;

  ctx->eglglesctx->egl_minor = 2;

  GST_INFO_OBJECT (ctx->element, "System reports supported EGL version v%d.%d",
      ctx->eglglesctx->egl_major, ctx->eglglesctx->egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup window/surface from handle");
  return FALSE;
}

/**
 * @brief: eglMakeCurrent 绑定 EGL 上下文（context）和渲染表面（surface）到当前线程（thread）
 * @param bind: TRUE表示绑定，FALSE表示解绑
*/
gboolean
gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx,
    gboolean bind)
{
  g_assert (ctx->display != NULL);

  if (bind && ctx->eglglesctx->surface && ctx->eglglesctx->eglcontext) {
    EGLContext *cur_ctx = eglGetCurrentContext ();

    if (cur_ctx == ctx->eglglesctx->eglcontext) {
      GST_DEBUG_OBJECT (ctx->element,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (ctx->element, "Attaching context to thread %p",
        g_thread_self ());

    g_print ("eglMakeCurrent  egldisplay = %p bind = %d\n", gst_egl_display_get (ctx->display), bind);
    g_print ("eglMakeCurrent  ctx->eglglesctx->surface = %p bind = %d\n", ctx->eglglesctx->surface, bind);
    g_print ("eglMakeCurrent  ctx->eglglesctx->eglcontext = %p bind = %d\n", ctx->eglglesctx->eglcontext, bind);

    if (!eglMakeCurrent (gst_egl_display_get (ctx->display),
            ctx->eglglesctx->surface, ctx->eglglesctx->surface,
            ctx->eglglesctx->eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (gst_egl_display_get (ctx->display),
            EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
      return FALSE;
    }
  }

  

  // 我的测试能否绑定成功
  // glFinish();
  
  // glBindTexture(GL_TEXTURE_2D, 1); 
  // got_egl_error ("glBindTexture");
  

  return TRUE;
}

/* XXX: Lock eglgles context? */
/**
 * @brief: 查询 EGLSurface 的长宽，赋值给 ctx->surface_width 和 ctx->surface_height
*/
gboolean
gst_egl_adaptation_update_surface_dimensions (GstEglAdaptationContext * ctx)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface, EGL_WIDTH, &width);
  eglQuerySurface (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface, EGL_HEIGHT, &height);

  if (width != ctx->surface_width || height != ctx->surface_height) {
    ctx->surface_width = width;
    ctx->surface_height = height;
    GST_INFO_OBJECT (ctx->element, "Got surface of %dx%d pixels", width,
        height);
    return TRUE;
  }

  return FALSE;
}

void
gst_egl_adaptation_bind_API (GstEglAdaptationContext * ctx)
{
  eglBindAPI (EGL_OPENGL_ES_API);
}

/**
 * @brief: eglSwapBuffers 交换egl前后缓存
*/
gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx, gchar* winsys,
        gpointer * own_window_data, GstBuffer * buf, gboolean show_latency)
{
#if USE_EGL_WAYLAND
  if (g_strcmp0(winsys, "wayland") == 0 && show_latency) {
    register_presentation_feedback(own_window_data, buf);
  }
#endif
  gboolean ret = eglSwapBuffers (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface);
  if (ret == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
  }
  return ret;
}

/**
 * @brief: eglChooseConfig 根据 eglChooseConfig 选择配置
 * @param num_configs(out): 给ctx->eglglesctx->config赋值了几组配置
*/
gboolean
_gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only,
    gint * num_configs)
{
  // EGLint cfg_number;
  // gboolean ret;
  // EGLConfig *config = NULL;

  // if (!try_only)
  //   config = &ctx->eglglesctx->config;

  // ret = eglChooseConfig (gst_egl_display_get (ctx->display),
  //     eglglessink_RGBA8888_attribs, config, 1, &cfg_number) != EGL_FALSE;

  // if (!ret)
  //   got_egl_error ("eglChooseConfig");
  // else if (num_configs)
  //   *num_configs = cfg_number;
  // return ret;

  GstEglGlesSink *sink = (GstEglGlesSink *)ctx->element;

  ctx->eglglesctx->config = sink->egl_config;
  if (num_configs)
    *num_configs = 1;

  return TRUE;
}

/**
 * @brief: eglCreateWindowSurface 创建一个egl表面
*/
gboolean
gst_egl_adaptation_create_surface (GstEglAdaptationContext * ctx)
{
  // ctx->eglglesctx->surface =
  //     eglCreateWindowSurface (gst_egl_display_get (ctx->display),
  //     ctx->eglglesctx->config, ctx->used_window, NULL);
  
  EGLint surface_attrs[] = {
    EGL_WIDTH, 1920,
    EGL_HEIGHT, 1080,
    EGL_NONE
  };

  ctx->eglglesctx->surface =
      eglCreatePbufferSurface ( gst_egl_display_get (ctx->display),
      ctx->eglglesctx->config, surface_attrs);

  if (ctx->eglglesctx->surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    return FALSE;
  }
  return TRUE;
}

/**
 * @brief: 查询surface是否保存上一帧，如果可以则 buffer_preserved = EGL_BUFFER_PRESERVED
 * @note:  如果你的应用程序在下一帧中需要访问前一帧的内容
 *         可以使用 EGL_BUFFER_PRESERVED 来确保在调用 eglSwapBuffers 后
 *         前一帧的缓冲区内容不会被破坏。这在某些情况下（例如进行增量绘制或需要参考前一帧内容的渲染操作）是非常有用的。
*/
void
gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx)
{
  EGLint swap_behavior;

  ctx->buffer_preserved = FALSE;
  if (eglQuerySurface (gst_egl_display_get (ctx->display),
          ctx->eglglesctx->surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (ctx->element, "Buffer swap behavior %x", swap_behavior);
    ctx->buffer_preserved = swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Can't query buffer swap behavior");
  }
}


/**
 * @brief: 查询并赋值像素缩放因子 
*/
void
gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx)
{
  EGLint display_par;

  /**
   * EGL_DISPLAY_SCALING 是 EGL 库中的一个常量，代表显示的缩放因子。
   * 该值通常用于表示显示器缩放的比例，用于在不同DPI（每英寸点数）和分辨率的屏幕上正确显示内容。
   * 
   * 这个常量的值是 10000，它用于表示缩放因子。
   * 例如，如果显示器的缩放因子是 1.0，那么 EGL_DISPLAY_SCALING 的值为 10000。
   * 如果缩放因子是 1.5，那么 EGL_DISPLAY_SCALING 的值为 15000。
  */
  ctx->pixel_aspect_ratio_d = EGL_DISPLAY_SCALING;

  /* 保存显示器的像素长宽比
   * 
   * DAR (显示宽高比) = w/h * EGL_DISPLAY_SCALING
   * 其中 EGL_DISPLAY_SCALING 是一个常量，值为 10000。
   * 该属性仅在 EGL 版本 >= 1.2 时支持。
   * XXX: 将其设置为一个属性或进行一次性检查。
   * 目前在每帧调用一次。
   * */
  if (ctx->eglglesctx->egl_major == 1 && ctx->eglglesctx->egl_minor < 2) {
    GST_DEBUG_OBJECT (ctx->element, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    ctx->pixel_aspect_ratio_n = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (ctx->element, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d", ctx->pixel_aspect_ratio_n,
          EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
      ctx->pixel_aspect_ratio_n = EGL_DISPLAY_SCALING;
    } else {
      ctx->pixel_aspect_ratio_n = display_par;
    }
  }
}

/**
 * @brief: 创建 eglCreateContext 上下文
*/
gboolean
gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx)
{
  // EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

  EGLint con_attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3, 
    EGL_CONTEXT_MINOR_VERSION, 2, 
    EGL_NONE 
  };
  GstEglGlesSink *sink = (GstEglGlesSink *)ctx->element;

  ctx->eglglesctx->eglcontext =
      eglCreateContext (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->config, sink->egl_share_context, con_attribs);
  
  ctx->egl_context = ctx->eglglesctx->eglcontext;
  
  g_print ("%s:  egl_display = %p\n",__func__, gst_egl_display_get (ctx->display));
  g_print ("%s:  egl_config = %p\n",__func__, ctx->eglglesctx->config);
  g_print ("%s:  egl_context = %p\n",__func__, sink->egl_share_context);
  g_print ("%s: NEW egl_context = %p\n\n",__func__, ctx->eglglesctx->eglcontext);

  if (ctx->eglglesctx->eglcontext == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x",
        eglGetError ());
    return FALSE;
  }

  GST_DEBUG_OBJECT (ctx->element, "EGL Context: %p",
      ctx->eglglesctx->eglcontext);

  return TRUE;
}


/**
 * @brief: 通过 @ctx 获取egl上下文
*/
EGLContext
gst_egl_adaptation_context_get_egl_context (GstEglAdaptationContext * ctx)
{
  g_return_val_if_fail (ctx != NULL, EGL_NO_CONTEXT);

  return ctx->eglglesctx->eglcontext;
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

/**
 * @brief: 销毁X11窗口，本质就是 
 *         XDestroyWindow (own_window_data->display, ctx->used_window);
 *         XCloseDisplay (own_window_data->display);
*/
void
gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx,
    gpointer * own_window_data, gchar* winsys)
{
#ifdef USE_EGL_WAYLAND
  if (g_strcmp0(winsys, "wayland") == 0) {
    platform_destroy_native_window_wayland (gst_egl_display_get
      (ctx->display), ctx->used_window, own_window_data);
    platform_destroy_display_wayland();
  }
#endif

#ifdef USE_EGL_X11
  if (g_strcmp0(winsys, "x11") == 0) {
    platform_destroy_native_window_x11 (gst_egl_display_get
        (ctx->display), ctx->used_window, own_window_data);
  }
#endif
}

/**
 * @brief: 创建X11下的窗口，本质就是 
 *         打开X显示 ((X11WindowData *)own_window_data)->display = XOpenDisplay
 *         创建X窗口 ctx->window =  XCreateSimpleWindow (...);
 * @param own_window_data(out): X显示的封装
*/
gboolean
gst_egl_adaptation_create_native_window (GstEglAdaptationContext * ctx,
    gint width, gint height, gpointer * own_window_data, gchar* winsys)
{
  GstEglGlesSink *sink = (GstEglGlesSink *) ctx->element;
  EGLNativeWindowType window = 0;

#ifdef USE_EGL_WAYLAND
  if (g_strcmp0(winsys, "wayland") == 0) {
    window =
      platform_create_native_window_wayland (sink->window_x,
                                             sink->window_y,
                                             width,
                                             height,
                                             sink->ivisurf_id,
                                             own_window_data);
  }
#endif

#ifdef USE_EGL_X11
  if (g_strcmp0(winsys, "x11") == 0) {
    window =
      platform_create_native_window_x11 (sink->window_x,
                                         sink->window_y,
                                         width,
                                         height,
                                         own_window_data);
  }
#endif

  if (window)
    gst_egl_adaptation_set_window (ctx, (uintptr_t) window);
  GST_DEBUG_OBJECT (ctx->element, "Using window handle %p", (gpointer) window);
  return window != 0;
}

/**
 * @brief: 给结构体GstEglAdaptationContext赋值： ctx->window
*/
void
gst_egl_adaptation_set_window (GstEglAdaptationContext * ctx, guintptr window)
{
  ctx->window = (EGLNativeWindowType)(uintptr_t) window;
}

/**
 * @brief: 给 GstEglGlesRenderContext（egl配置、上下文、表面） 结构体分配内存
*/
void
gst_egl_adaptation_init (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx = g_new0 (GstEglGlesRenderContext, 1);
}

/**
 * @brief: GstEglGlesRenderContext（egl配置、上下文、表面）释放内存
*/
void
gst_egl_adaptation_deinit (GstEglAdaptationContext * ctx)
{
  g_free (ctx->eglglesctx);
}

void
gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->surface) {
    eglDestroySurface (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->surface);
    ctx->eglglesctx->surface = NULL;
    ctx->have_surface = FALSE;
  }
}

void
gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->eglcontext) {
    eglDestroyContext (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->eglcontext);
    ctx->eglglesctx->eglcontext = NULL;
  }
}

void
gst_egl_adaptation_release_thread ()
{
  eglReleaseThread ();
}
