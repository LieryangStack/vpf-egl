/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2015-2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __GST_EGLGLESSINK_H__
#define __GST_EGLGLESSINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/base/gstdataqueue.h>

#include <cuda.h>
#include <cudaGL.h>
#include <cuda_runtime.h>

#include "gstegladaptation.h"
#include "gstegljitter.h"

G_BEGIN_DECLS
#define GST_TYPE_EGLGLESSINK \
  (gst_eglglessink_get_type())
#define GST_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EGLGLESSINK,GstEglGlesSink))
#define GST_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EGLGLESSINK,GstEglGlesSinkClass))
#define GST_IS_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EGLGLESSINK))
#define GST_IS_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EGLGLESSINK))

typedef struct _GstEglGlesSink GstEglGlesSink;
typedef struct _GstEglGlesSinkClass GstEglGlesSinkClass;

/*
 * GstEglGlesSink:
 * @format: Caps' video format field
 * @display_region: Surface region to use as rendering canvas
 * @sinkcaps: Full set of suported caps
 * @current_caps: Current caps
 * @rendering_path: Rendering path (Slow/Fast)
 * @eglglesctx: Pointer to the associated EGL/GLESv2 rendering context
 * @flow_lock: Simple concurrent access ward to the sink's runtime state
 * @have_window: Set if the sink has access to a window to hold it's canvas
 * @using_own_window: Set if the sink created its own window
 * @egl_started: Set if the whole EGL setup has been performed
 * @create_window: Property value holder to allow/forbid internal window creation
 * @force_rendering_slow: Property value holder to force slow rendering path
 * @force_aspect_ratio: Property value holder to consider PAR/DAR when scaling
 *
 * The #GstEglGlesSink data structure.
 */
struct _GstEglGlesSink
{
  GstVideoSink videosink;       /* Element hook */

  /* Region of the surface that should be rendered */
  GstVideoRectangle render_region;
  gboolean render_region_changed;
  gboolean render_region_user;

  /* Region of render_region that should be filled
   * with the video frames */
  GstVideoRectangle display_region;

  GstVideoRectangle crop;
  gboolean crop_changed;
  GstCaps *sinkcaps;
  GstCaps *current_caps, *configured_caps; 
  GstVideoInfo configured_info;
  gfloat stride[3];
  GstVideoGLTextureOrientation orientation;
#ifndef HAVE_IOS
  GstBufferPool *pool;
#endif

  GstEglAdaptationContext *egl_context;
  gint window_x;
  gint window_y;
  gint window_width;
  gint window_height;
  guint profile;
  gint rows;
  gint columns;
  gint change_port;

  /* Runtime flags */
  gboolean have_window; /* 是否成功创建了X11窗口 */
  gboolean using_own_window;
  gboolean egl_started; /* egl成功在线程中初始化 */
  gboolean is_closing; /* egl是否关闭flag */
  gboolean using_cuda; /* GPU CUDA */
  gboolean using_nvbufsurf; /* Jetson */

  gpointer own_window_data; /* X11窗口下的 Display */
  GMutex window_lock;

  GThread *thread;
  gboolean thread_running;
  GstDataQueue *queue; /* 需要处理数据的队列 */
  GCond render_exit_cond; 
  GCond render_cond;
  GMutex render_lock;
  GstFlowReturn last_flow;
  GstMiniObject *dequeued_object;  /* 当前那个元素出队（从@queue），也就是该元素被处理了 */
  GThread *event_thread; /* X11窗口事件线程 */

  GstBuffer *last_buffer;

  EGLNativeDisplayType display;

  GstEglJitterTool *pDeliveryJitter;

  /* Properties */
  gboolean create_window;
  gboolean force_aspect_ratio;
  gchar* winsys; /* 使用了那个窗口类型，比如 winsys = "x11" */
  gboolean show_latency;

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

  GstBuffer *last_uploaded_buffer; /* 最近一次更新的buffer（上传纹理成功后会更新） */
  CUcontext cuContext; /* CDUA 上下文 */
  CUgraphicsResource cuResource[3]; /* CUDA资源 */
  unsigned int gpu_id;
  gboolean nvbuf_api_version_new;
  unsigned int ivisurf_id;

  /*
    Pointer to a SW Buffer. This is needed in case of RGB/BGR as Cuda
    doesn't support 3-channel formats. The cuda buffer (host or device)
    is copied using cuMemCpy2D into this sw buffer and then fill the GL
    texture from the SW buffer.
  */
  uint8_t *swData;

  /* 自定义参数 */
  EGLDisplay egl_display;
  EGLConfig egl_config;
  EGLContext egl_share_context; /* 来自UI线程的egl上下文 */
  guint egl_share_texture; /* 来自UI线程的共享纹理ID */
};

struct _GstEglGlesSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_eglglessink_get_type (void);

G_END_DECLS
#endif /* __GST_EGLGLESSINK_H__ */
