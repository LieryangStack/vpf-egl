#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-frame.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/videooverlay.h>

#include "gstegladaptation.h"
#include "gsteglglessink.h"
#include "gstegljitter.h"
#include "config.h"

#include <gtk/gtk.h>

#include "nvbufsurface.h"


#ifdef IS_DESKTOP
#define DEFAULT_NVBUF_API_VERSION_NEW   TRUE
#else
#define DEFAULT_NVBUF_API_VERSION_NEW   FALSE
#endif


GST_DEBUG_CATEGORY_STATIC (gst_eglglessink_debug);
#define GST_CAT_DEFAULT gst_eglglessink_debug
#ifdef IS_DESKTOP
#define DEFAULT_GPU_ID 0
#endif

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

/* Input capabilities. */
static GstStaticPadTemplate gst_eglglessink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
#ifndef HAVE_IOS
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE,
            "{ " "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }") ";"
#endif
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            "{ " "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }") ";"
        GST_VIDEO_CAPS_MAKE ("{ "
            "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }")
            ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:NVMM",
            "{ " "BGRx, RGBA, I420, NV12, BGR, RGB }")
        ));

/* Filter signals and args */
enum
{
  PAINTABLE,
  /* FILL ME */
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_0,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
  PROP_DISPLAY,
  PROP_WINDOW_X,
  PROP_WINDOW_Y,
  PROP_WINDOW_WIDTH,
  PROP_WINDOW_HEIGHT,
#ifdef IS_DESKTOP
  PROP_GPU_DEVICE_ID,
#endif
  PROP_ROWS,
  PROP_COLUMNS,
  PROP_PROFILE,
  PROP_WINSYS,
  PROP_SHOW_LATENCY,
  PROP_NVBUF_API_VERSION,
  PROP_IVI_SURF_ID,

  PROP_EGL_DISPLAY,
  PROP_EGL_CONFIG,
  PROP_EGL_SHARE_CONTEXT,
  PROP_EGL_SHARE_TEXTURE
};

static void 
gst_eglglessink_finalize (GObject * object);
static void 
gst_eglglessink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void 
gst_eglglessink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn 
gst_eglglessink_change_state (GstElement * element, GstStateChange transition);
static void 
gst_eglglessink_set_context (GstElement * element, GstContext * context);
static GstFlowReturn 
gst_eglglessink_prepare (GstBaseSink * bsink, GstBuffer * buf);
static GstFlowReturn 
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf);
static gboolean 
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps 
*gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter);
static gboolean 
gst_eglglessink_propose_allocation (GstBaseSink * bsink, GstQuery * query);
static gboolean 
gst_eglglessink_query (GstBaseSink * bsink, GstQuery * query);


/* Utility */
static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps);
static gboolean
gst_eglglessink_cuda_init(GstEglGlesSink * eglglessink);
static void
gst_eglglessink_cuda_cleanup(GstEglGlesSink * eglglessink);
static gboolean
gst_eglglessink_cuda_buffer_copy(GstEglGlesSink * eglglessink, GstBuffer * buf);
static GstFlowReturn 
gst_eglglessink_upload (GstEglGlesSink * sink, GstBuffer * buf);
static GstFlowReturn 
gst_eglglessink_render (GstEglGlesSink * sink);
static GstFlowReturn 
gst_eglglessink_queue_object (GstEglGlesSink * sink, GstMiniObject * obj);
static inline gboolean 
egl_init (GstEglGlesSink * eglglessink);


typedef GstBuffer *(*GstEGLImageBufferPoolSendBlockingAllocate) (GstBufferPool *pool, gpointer data);


G_DECLARE_FINAL_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool, GST, EGL_IMAGE_BUFFER_POOL, GstVideoBufferPool);


/* EGLImage memory, buffer pool, etc */
struct _GstEGLImageBufferPool {

  GstVideoBufferPool parent;

  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoInfo info;  /* 视频帧的格式，长宽等信息 */
  gboolean add_metavideo;
  gboolean want_eglimage;
  GstBuffer *last_buffer;
  GstEGLImageBufferPoolSendBlockingAllocate send_blocking_allocate_func;
  gpointer send_blocking_allocate_data;
  GDestroyNotify send_blocking_allocate_destroy;
};


/* 定义了一个继承于 GstVideoBufferPool 的 GstEGLImageBufferPool 对象 */
G_DEFINE_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool, GST_TYPE_VIDEO_BUFFER_POOL);


static GstBufferPool* 
gst_egl_image_buffer_pool_new (GstEGLImageBufferPoolSendBlockingAllocate blocking_allocate_func, 
                               gpointer blocking_allocate_data, 
                               GDestroyNotify destroy_func);


/**
 * @brief: 获取视频帧的格式、长宽信息
 * @param format(out): 视频帧的格式
 * @param width(out):  视频帧的宽度
 * @param height(out): 视频帧的高度
*/
static void
gst_egl_image_buffer_pool_get_video_infos (GstEGLImageBufferPool * pool,
                                           GstVideoFormat * format, gint * width, gint * height) {

  g_return_if_fail (pool != NULL);

  if (format)
    *format = pool->info.finfo->format;

  if (width)
    *width = pool->info.width;

  if (height)
    *height = pool->info.height;
}


/**
 * @brief: 将@buffer替换pool->last_buffer，其实就是 pool->last_buffer = buffer
*/
static void
gst_egl_image_buffer_pool_replace_last_buffer (GstEGLImageBufferPool * pool,
    GstBuffer * buffer)
{
  g_return_if_fail (pool != NULL);

  gst_buffer_replace (&pool->last_buffer, buffer);
}



/**
 * @brief: 
 */
static GstBuffer *
gst_eglglessink_egl_image_buffer_pool_send_blocking (GstBufferPool * bpool,
                                                     gpointer data) {
  
  GstFlowReturn ret = GST_FLOW_OK;
  GstQuery *query = NULL;
  GstStructure *s = NULL;
  const GValue *v = NULL;
  GstBuffer *buffer = NULL;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  gint width = 0;
  gint height = 0;

  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (data);

  gst_egl_image_buffer_pool_get_video_infos (pool, &format, &width, &height);

  s = gst_structure_new ("eglglessink-allocate-eglimage",
      "format", GST_TYPE_VIDEO_FORMAT, format,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
  query = gst_query_new_custom (GST_QUERY_CUSTOM, s);

  /* 这会调用 render_thread_func 函数，进行查询处理，然后获取到创建的buffer  */
  ret = gst_eglglessink_queue_object (eglglessink, GST_MINI_OBJECT_CAST (query));

  if (ret == GST_FLOW_OK && gst_structure_has_field (s, "buffer")) {
    v = gst_structure_get_value (s, "buffer");
    buffer = GST_BUFFER_CAST (g_value_get_pointer (v));
  }

  gst_query_unref (query);

  return buffer;
}

static void
gst_eglglessink_egl_image_buffer_pool_on_destroy (gpointer data)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (data);
  gst_object_unref (eglglessink);
}

static const gchar **
gst_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

/**
 * @brief: 通过 @config 获取到 GstCaps，从而获取到 GstVideoInfo，设定 pool->info = info
*/
static gboolean
gst_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
                                      GstStructure * config) {

  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstCaps *caps;
  GstVideoInfo info;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (!GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->set_config (bpool, config))
    return FALSE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (!gst_buffer_pool_config_get_allocator (config, &pool->allocator, &pool->params))
    return FALSE;


  pool->add_metavideo = gst_buffer_pool_config_has_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  pool->want_eglimage = (pool->allocator && g_strcmp0 (pool->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

  pool->info = info;

  return TRUE;
}


static GstFlowReturn
gst_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
                                        GstBuffer ** buffer, 
                                        GstBufferPoolAcquireParams * params) {
  
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  *buffer = NULL;

  /* add_metavideo 是什么？， want_eglimage 是TRUE  */
  /**
   * add_metavideo
   * want_eglimage 是 TRUE
   */
  if (!pool->add_metavideo || !pool->want_eglimage)
    return GST_BUFFER_POOL_CLASS (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool, buffer, params);

  if (!pool->allocator)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (pool->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      
      /* 调用的是 gst_eglglessink_egl_image_buffer_pool_send_blocking 函数 */
      if (pool->send_blocking_allocate_func)
        *buffer = pool->send_blocking_allocate_func (bpool, pool->send_blocking_allocate_data);

      if (!*buffer) {
        GST_WARNING ("Fallback memory allocation");
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool, buffer, params);
      }

      return GST_FLOW_OK;
      break;
    }
    default:
      return
          GST_BUFFER_POOL_CLASS
          (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);
      break;
  }

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: 不要返回我们刚刚渲染的内存，glEGLImageTargetTexture2DOES()
  * 保持 EGLImage 不可映射，直到下一个图像被上传
  */
  if (*buffer && *buffer == pool->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret = GST_BUFFER_POOL_CLASS (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool, buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_egl_image_buffer_pool_finalize (GObject * object)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (object);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  gst_egl_image_buffer_pool_replace_last_buffer (pool, NULL);

  if (pool->send_blocking_allocate_destroy)
    pool->send_blocking_allocate_destroy (pool->send_blocking_allocate_data);
  pool->send_blocking_allocate_destroy = NULL;
  pool->send_blocking_allocate_data = NULL;

  G_OBJECT_CLASS (gst_egl_image_buffer_pool_parent_class)->finalize (object);
}

static void
gst_egl_image_buffer_pool_class_init (GstEGLImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options = gst_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_egl_image_buffer_pool_alloc_buffer; /* GstBufferPoolClass分配buffer */
  gstbufferpool_class->acquire_buffer =  gst_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_egl_image_buffer_pool_init (GstEGLImageBufferPool * pool)
{
}



/* 定义了继承于 GstVideoSink 的 GstEglGlesSink 对象  */
#define parent_class gst_eglglessink_parent_class
G_DEFINE_TYPE (GstEglGlesSink, gst_eglglessink, GST_TYPE_VIDEO_SINK);

/**
 * @brief: 1. 检查gtk是否初始化
 *         2. 设定EGLDisplay变量给GstEGLDisplay结构体
 */
static inline gboolean
egl_init (GstEglGlesSink * eglglessink) {

  GstCaps *caps;
  GError *error = NULL;

  /* 如果主程序没有初始化gtk，这里初始化gtk */
  if (!gtk_init_check())
    gtk_init ();

  GdkGLContext *context = gdk_display_create_gl_context(gdk_display_get_default(), &error);
  if (error) {
    g_message ("%s\n",error->message);
    g_clear_error (&error);
  }

  /* 创建 paintable */
  eglglessink->paintable = gtk_gst_paintable_new ();
  gtk_gst_paintable_set_context (GTK_GST_PAINTABLE(eglglessink->paintable), context);
  gtk_gst_paintable_set_sink (GTK_GST_PAINTABLE(eglglessink->paintable), GST_ELEMENT(eglglessink));

  /* 把新创建的egl_context设定为当前线程上下文 */
  gdk_gl_context_make_current (context);

  /* 创建纹理 */
  eglglessink->egl_context->n_textures = 1;
  glGenTextures(1, eglglessink->egl_context->texture);
  glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0); 


  g_signal_emit (eglglessink, signals[PAINTABLE], 0, eglglessink->paintable);



  /* EGLDisplay 包装了一下， ctx->display = display; */
  eglglessink->egl_context->display = gst_egl_display_new (eglGetCurrentDisplay (), NULL);

  /* 清除当前线程的egl上下文绑定 */
  gdk_gl_context_clear_current();

  /* 添加支持的媒体类型 */
  caps =
      gst_egl_adaptation_fill_supported_fbuffer_configs
      (eglglessink->egl_context);
  if (!caps) {
    GST_ERROR_OBJECT (eglglessink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  } else {
    GST_OBJECT_LOCK (eglglessink);
    gst_caps_replace (&eglglessink->sinkcaps, caps);
    GST_OBJECT_UNLOCK (eglglessink);
    gst_caps_unref (caps);
  }

  eglglessink->egl_started = TRUE;

  eglglessink->glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
      eglGetProcAddress ("glEGLImageTargetTexture2DOES");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Failed to perform EGL init");
  return FALSE;
}


static gboolean
gtk_gst_paintable_set_texture_invoke (gpointer data)
{
  GstEglGlesSink * eglglessink = GST_EGLGLESSINK(data);

  gdk_paintable_invalidate_contents (eglglessink->paintable);

  return G_SOURCE_REMOVE;
}


/**
 * @brief: 处理 GstBuffer， 然后进行渲染
*/
static gpointer
render_thread_func (GstEglGlesSink * eglglessink) {

  GstMessage *message;
  GValue val = { 0 };
  GstDataQueueItem *item = NULL;
  GstFlowReturn last_flow = GST_FLOW_OK;

  /* CUDA设定GPU */
  cudaError_t CUerr = cudaSuccess;
  GST_LOG_OBJECT (eglglessink, "SETTING CUDA DEVICE = %d in eglglessink func=%s\n", eglglessink->gpu_id, __func__);
  CUerr = cudaSetDevice(eglglessink->gpu_id);
  if (CUerr != cudaSuccess) {
    GST_LOG_OBJECT (eglglessink,"\n *** Unable to set device in %s Line %d\n", __func__, __LINE__);
    return NULL;
  }

  /* 发送message，告诉应用程序，已经成功 创建流stream线程 */
  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting ENTER stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  eglBindAPI (EGL_OPENGL_ES_API);

  /**
   * 如果队列中没有元素，就会阻塞等待（如果设定数据队列为刷新状态，该函数就会立即返回FALSE）
   */
  while (gst_data_queue_pop (eglglessink->queue, &item)) {
    GstMiniObject *object = item->object;

    GST_DEBUG_OBJECT (eglglessink, "Handling object %" GST_PTR_FORMAT, object);

    if (GST_IS_CAPS (object)) {  /* 如果接收到的是GstCaps */
      GstCaps *caps = GST_CAPS_CAST (object);

      if (caps != eglglessink->configured_caps) {
        if (!gst_eglglessink_configure_caps (eglglessink, caps)) {
          last_flow = GST_FLOW_NOT_NEGOTIATED;
        }
      }
    } else if (GST_IS_QUERY (object)) {   /* 如果是接收到GstQuery查询，其实这一步是被 GstBufferPool创建Buffer，然后调用gst_eglglessink_egl_image_buffer_pool_send_blocking  */
      GstQuery *query = GST_QUERY_CAST (object);
      GstStructure *s = (GstStructure *) gst_query_get_structure (query);

      if (gst_structure_has_name (s, "eglglessink-allocate-eglimage")) {
        GstBuffer *buffer;
        GstVideoFormat format;
        gint width, height;
        GValue v = { 0, };

        if (!gst_structure_get_enum (s, "format", GST_TYPE_VIDEO_FORMAT,
                (gint *) & format)
            || !gst_structure_get_int (s, "width", &width)
            || !gst_structure_get_int (s, "height", &height)) {
          g_assert_not_reached ();
        }

        buffer = gst_egl_image_allocator_alloc_eglimage (GST_EGL_IMAGE_BUFFER_POOL(eglglessink->pool)->allocator, 
                                                         eglglessink->egl_context->display,
                                                         eglGetCurrentContext(), 
                                                         format, width, height);

        g_value_init (&v, G_TYPE_POINTER);
        g_value_set_pointer (&v, buffer);
        gst_structure_set_value (s, "buffer", &v);
        g_value_unset (&v);
      } else {
        g_assert_not_reached ();
      }
      last_flow = GST_FLOW_OK;
    } else if (GST_IS_BUFFER (object)) { /* 如果接收到 GstBuffer  */
      GstBuffer *buf = GST_BUFFER_CAST (item->object);

      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_upload (eglglessink, buf); /* 将GPU内部的纹理更新到我们创建的纹理 eglglessink->egl_context->texture[0] */
        if (last_flow == GST_FLOW_OK)
            // gdk_paintable_invalidate_contents (eglglessink->paintable);
              g_main_context_invoke_full (NULL,
                              G_PRIORITY_DEFAULT,
                              gtk_gst_paintable_set_texture_invoke,
                              eglglessink, NULL);
          // g_signal_emit (eglglessink, signals[UI_RENDER], 0);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else if (!object) {  /* 如果是 object == NULL */
      if (eglglessink->configured_caps) {
        
        last_flow = gst_eglglessink_render (eglglessink);  /* 绘制OpenGL ES顶点 */

        if (eglglessink->last_uploaded_buffer && eglglessink->pool) {
          gst_egl_image_buffer_pool_replace_last_buffer (GST_EGL_IMAGE_BUFFER_POOL
              (eglglessink->pool), eglglessink->last_uploaded_buffer);
          eglglessink->last_uploaded_buffer = NULL;
        }

        if (eglglessink->last_uploaded_buffer && eglglessink->using_nvbufsurf) {
            GstMapInfo map = { NULL, (GstMapFlags) 0, NULL, 0, 0, };
            GstMemory *mem = gst_buffer_peek_memory (eglglessink->last_uploaded_buffer, 0);
            gst_memory_map (mem, &map, GST_MAP_READ);

            NvBufSurface *in_surface = (NvBufSurface*) map.data;

            if (NvBufSurfaceUnMapEglImage (in_surface, 0) !=0) {
                GST_ERROR_OBJECT (eglglessink, "ERROR: NvBufSurfaceUnMapEglImage\n");
            }

            gst_memory_unmap (mem, &map);
        }

      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = last_flow;
    eglglessink->dequeued_object = object;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);

    if (last_flow != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (eglglessink, "Successfully handled object");
  }

  if (eglglessink->last_uploaded_buffer && eglglessink->pool) {
    gst_egl_image_buffer_pool_replace_last_buffer (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool), eglglessink->last_uploaded_buffer);
    eglglessink->last_uploaded_buffer = NULL;
  }

  if (last_flow == GST_FLOW_OK) {
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = GST_FLOW_FLUSHING;
    eglglessink->dequeued_object = NULL;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);
  }

  GST_DEBUG_OBJECT (eglglessink, "Shutting down thread");

  /* EGL/GLES cleanup */
  g_mutex_lock (&eglglessink->render_lock);
  if (!eglglessink->is_closing) {
    g_cond_wait (&eglglessink->render_exit_cond, &eglglessink->render_lock);
  }
  g_mutex_unlock (&eglglessink->render_lock);

  if (eglglessink->using_cuda) {
    gst_eglglessink_cuda_cleanup(eglglessink);
  }

  // gst_egl_adaptation_cleanup (eglglessink->egl_context);

  if (eglglessink->configured_caps) {
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  // gst_egl_adaptation_release_thread ();

  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting LEAVE stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  return NULL;
}

/**
 * @brief: 元素@eglglessink从READY_TO_PAUSED状态的时候，会调用该函数
 * @note: 该函数会启用一个线程，渲染线程
*/
static gboolean
gst_eglglessink_start (GstEglGlesSink * eglglessink)
{
  GError *error = NULL;
  cudaError_t CUerr = cudaSuccess;

  GST_DEBUG_OBJECT (eglglessink, "Starting");

  /* 检查渲染线程是否已经被创建，如果处于阻塞状态，发送广播条件，等待线程结束 */
  if (eglglessink->thread) {
    g_cond_broadcast (&eglglessink->render_exit_cond);
    g_thread_join (eglglessink->thread);
    eglglessink->thread = NULL;
  }

  /* 检查egl是否已经初始化完毕 */
  if (!eglglessink->egl_started) {
    GST_ERROR_OBJECT (eglglessink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  eglglessink->last_flow = GST_FLOW_OK;
  eglglessink->display_region.w = 0;
  eglglessink->display_region.h = 0;
  eglglessink->is_closing = FALSE;

  if (!g_strcmp0 (g_getenv("DS_NEW_BUFAPI"), "1")){
    eglglessink->nvbuf_api_version_new = TRUE;
  }

  /* 如果设定为刷新状态，任何传入的数据都会被丢弃，调用数据队列push和pop的阻塞函数会返回FALSE */
  gst_data_queue_set_flushing (eglglessink->queue, FALSE);

  GST_LOG_OBJECT (eglglessink, "SETTING CUDA DEVICE = %d in eglglessink func=%s\n", eglglessink->gpu_id, __func__);
  CUerr = cudaSetDevice(eglglessink->gpu_id);
  if (CUerr != cudaSuccess) {
    GST_LOG_OBJECT (eglglessink,"\n *** Unable to set device in %s Line %d\n", __func__, __LINE__);
    goto HANDLE_ERROR;
  }

#if !GLIB_CHECK_VERSION (2, 31, 0)
  eglglessink->thread =
      g_thread_create ((GThreadFunc) render_thread_func, eglglessink, TRUE,
      &error);
#else
  eglglessink->thread = g_thread_try_new ("eglglessink-render",
      (GThreadFunc) render_thread_func, eglglessink, &error);
#endif

  if (!eglglessink->thread || error != NULL)
    goto HANDLE_ERROR;

  GST_DEBUG_OBJECT (eglglessink, "Started");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't start");
  g_clear_error (&error);
  return FALSE;
}


static gboolean
gst_eglglessink_stop (GstEglGlesSink * eglglessink)
{
  g_print ("gst_eglglessink_stop (GstEglGlesSink * eglglessink)\n");
  GST_DEBUG_OBJECT (eglglessink, "Stopping");

  gst_data_queue_set_flushing (eglglessink->queue, TRUE);
  g_mutex_lock (&eglglessink->render_lock);
  g_cond_broadcast (&eglglessink->render_cond);
  g_mutex_unlock (&eglglessink->render_lock);

  eglglessink->last_flow = GST_FLOW_FLUSHING;

#ifndef HAVE_IOS
  if (eglglessink->pool)
    gst_egl_image_buffer_pool_replace_last_buffer (GST_EGL_IMAGE_BUFFER_POOL
        (eglglessink->pool), NULL);
#endif

  if (eglglessink->current_caps) {
    gst_caps_unref (eglglessink->current_caps);
    eglglessink->current_caps = NULL;
  }

  GST_DEBUG_OBJECT (eglglessink, "Stopped");

  return TRUE;
}


static void
queue_item_destroy (GstDataQueueItem * item)
{
  if (item->object && !GST_IS_QUERY (item->object))
    gst_mini_object_unref (item->object);
  g_slice_free (GstDataQueueItem, item);
}

/**
 * @brief: 这个函数就是通过 eglglessink->queue 实现跟渲染线程的数据传输，让渲染线程处理 @obj
 * @note: 这个函数的执行完毕，必须等到渲染 render_thread_func 线程处理@obj完毕（阻塞等待渲染线程处理完@obj）
 * 
 *       分析： obj = NULL 的情况
*/
static GstFlowReturn
gst_eglglessink_queue_object (GstEglGlesSink * eglglessink, GstMiniObject * obj)
{
  GstDataQueueItem *item;
  GstFlowReturn last_flow;

  g_mutex_lock (&eglglessink->render_lock);
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  if (last_flow != GST_FLOW_OK)
    return last_flow;

  /* 创建了一个 GstDataQueueItem 对象（非标准GstObject） */
  item = g_slice_new0 (GstDataQueueItem);

  if (obj == NULL)
    item->object = NULL;
  else if (GST_IS_QUERY (obj))
    item->object = obj;
  else
    item->object = gst_mini_object_ref (obj);
  
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) queue_item_destroy;

  GST_DEBUG_OBJECT (eglglessink, "Queueing object %" GST_PTR_FORMAT, obj);

  g_mutex_lock (&eglglessink->render_lock);
  if (!gst_data_queue_push (eglglessink->queue, item)) {
    item->destroy (item);
    g_mutex_unlock (&eglglessink->render_lock);
    GST_DEBUG_OBJECT (eglglessink, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  GST_DEBUG_OBJECT (eglglessink, "Waiting for object to be handled");

  /**
   * 会一直等待 gst_data_queue_push 压入的 item 被处理，
  */
  do {
    g_cond_wait (&eglglessink->render_cond, &eglglessink->render_lock);
  } while (eglglessink->dequeued_object != obj
      && eglglessink->last_flow != GST_FLOW_FLUSHING);
  
  GST_DEBUG_OBJECT (eglglessink, "Object handled: %s",
      gst_flow_get_name (eglglessink->last_flow));
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  return (obj ? last_flow : GST_FLOW_OK);
}


static gboolean
gst_eglglessink_crop_changed (GstEglGlesSink * eglglessink,
    GstVideoCropMeta * crop)
{
  if (crop) {
    return (crop->x != (guint)eglglessink->crop.x ||
        crop->y != (guint)eglglessink->crop.y ||
        crop->width != (guint)eglglessink->crop.w ||
        crop->height != (guint)eglglessink->crop.h);
  }

  return (eglglessink->crop.x != 0 || eglglessink->crop.y != 0 ||
      eglglessink->crop.w != eglglessink->configured_info.width ||
      eglglessink->crop.h != eglglessink->configured_info.height);
}

static gboolean
gst_eglglessink_fill_texture (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstVideoFrame vframe;
#ifndef GST_DISABLE_GST_DEBUG
  gint w;
#endif
  gint h;

  memset (&vframe, 0, sizeof (vframe));

  if (!gst_video_frame_map (&vframe, &eglglessink->configured_info, buf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't map frame");
    goto HANDLE_ERROR;
  }
#ifndef GST_DISABLE_GST_DEBUG
  w = GST_VIDEO_FRAME_WIDTH (&vframe);
#endif
  h = GST_VIDEO_FRAME_HEIGHT (&vframe);

  GST_DEBUG_OBJECT (eglglessink,
      "Got buffer %p: %dx%d size %" G_GSIZE_FORMAT, buf, w, h,
      gst_buffer_get_size (buf));

  switch (eglglessink->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w * 3 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width * 3 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_SHORT_5_6_5, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_AYUV:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 1));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 2);

      glActiveTexture (GL_TEXTURE2);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[2] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 2),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 2));
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride / 2;

        if (GST_ROUND_UP_8 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1));
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glTexImage2D"))
    goto HANDLE_ERROR;

  gst_video_frame_unmap (&vframe);

  return TRUE;

HANDLE_ERROR:
  {
    if (vframe.buffer)
      gst_video_frame_unmap (&vframe);
    return FALSE;
  }
}

static gboolean
gst_eglglessink_cuda_buffer_copy (GstEglGlesSink * eglglessink, GstBuffer * buf) {

  CUarray dpArray;
  CUresult result;
  guint width, height;
  GstMapInfo info = GST_MAP_INFO_INIT;
  GstVideoFormat videoFormat;
  int is_v4l2_mem = 0;
  GstMemory *inMem;
  NvBufSurface *in_surface = NULL;

  width = GST_VIDEO_SINK_WIDTH (eglglessink);
  height = GST_VIDEO_SINK_HEIGHT (eglglessink);

  result = cuCtxSetCurrent(eglglessink->cuContext);
  if (result != CUDA_SUCCESS) {
    g_print ("cuCtxSetCurrent failed with error(%d) %s\n", result, __func__);
    return FALSE;
  }
  gst_buffer_map (buf, &info, GST_MAP_READ);

  //Checking for V4l2Memory
  inMem = gst_buffer_peek_memory (buf, 0);
  if (!g_strcmp0 (inMem->allocator->mem_type, "V4l2Memory"))
    is_v4l2_mem = 1;

  gst_buffer_unmap (buf, &info);

  if ((!is_v4l2_mem && info.size != sizeof(NvBufSurface)) || (is_v4l2_mem && !eglglessink->nvbuf_api_version_new)) {
    g_print ("nveglglessink cannot handle Legacy NVMM Buffers %s\n", __func__);
    return FALSE;
  }

  in_surface = (NvBufSurface*) info.data;

  if (in_surface->batchSize != 1) {
    g_print ("ERROR: Batch size not 1\n");
    return FALSE;
  }

  NvBufSurfaceMemType memType = in_surface->memType;
  gboolean is_device_memory = FALSE;
  gboolean is_host_memory = FALSE;
  if (memType == NVBUF_MEM_DEFAULT) {
#ifdef IS_DESKTOP
    memType = NVBUF_MEM_CUDA_DEVICE; /* GPU平台 */
#else
    memType = NVBUF_MEM_SURFACE_ARRAY;  /* Jetson */
#endif
  }

  if (memType == NVBUF_MEM_SURFACE_ARRAY || memType == NVBUF_MEM_HANDLE) {
    g_print ("eglglessink cannot handle NVRM surface array %s\n", __func__);
    return FALSE;
  }

  if (memType == NVBUF_MEM_CUDA_DEVICE || memType == NVBUF_MEM_CUDA_UNIFIED) {
    is_device_memory = TRUE;
  }
  else if (memType == NVBUF_MEM_CUDA_PINNED) {
    is_host_memory = TRUE;
  }

  CUDA_MEMCPY2D m = { 0 };

  videoFormat = eglglessink->configured_info.finfo->format;
  switch (videoFormat) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB: {
      gint bytesPerPix = 3;

      uint8_t *ptr = (uint8_t *)in_surface->surfaceList[0].dataPtr;

      if (is_device_memory) {
        m.srcDevice = (CUdeviceptr) ptr;
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      }
      else if (is_host_memory) {
        m.srcHost = (void *)ptr;
        m.srcMemoryType = CU_MEMORYTYPE_HOST;
      }
      m.srcPitch = in_surface->surfaceList[0].planeParams.pitch[0];
      m.dstHost = (void *)eglglessink->swData;
      m.dstMemoryType = CU_MEMORYTYPE_HOST;
      m.dstPitch = width * bytesPerPix;
      m.Height = height;
      m.WidthInBytes = width * bytesPerPix;

      result = cuMemcpy2D(&m);
      if (result != CUDA_SUCCESS) {
        g_print ("cuMemcpy2D failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      glActiveTexture (GL_TEXTURE0);
      glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
          GL_UNSIGNED_BYTE, (void *)eglglessink->swData);
      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
     }
     break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx: {
      gint bytesPerPix = 4;
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);

      result = cuGraphicsMapResources(1, &(eglglessink->cuResource[0]), 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsMapResources failed with error(%d) %s\n", result, __func__);
        return FALSE;
      }
      result = cuGraphicsSubResourceGetMappedArray(&dpArray, eglglessink->cuResource[0], 0, 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsResourceGetMappedPointer failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      if (is_device_memory) {
        m.srcDevice = (CUdeviceptr) in_surface->surfaceList[0].dataPtr;
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      }
      else if (is_host_memory) {
        m.srcHost = (void *)in_surface->surfaceList[0].dataPtr;
        m.srcMemoryType = CU_MEMORYTYPE_HOST;
      }

      m.srcPitch = in_surface->surfaceList[0].planeParams.pitch[0];

      m.dstPitch = width * bytesPerPix;
      m.WidthInBytes = width * bytesPerPix;

      m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
      m.dstArray = dpArray;
      m.Height = height;

      result = cuMemcpy2D(&m);
      if (result != CUDA_SUCCESS) {
        g_print ("cuMemcpy2D failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      result = cuGraphicsUnmapResources(1, &(eglglessink->cuResource[0]), 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsUnmapResources failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;

     } // case RGBA
     break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12: {
      uint8_t *ptr;
      int i, pstride;
      int num_planes = (int)in_surface->surfaceList[0].planeParams.num_planes;

      for ( i = 0; i < num_planes; i ++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);

        result = cuGraphicsMapResources(1, &(eglglessink->cuResource[i]), 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsMapResources failed with error(%d) %s\n", result, __func__);
          return FALSE;
        }
        result = cuGraphicsSubResourceGetMappedArray(&dpArray, eglglessink->cuResource[i], 0, 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsResourceGetMappedPointer failed with error(%d) %s\n", result, __func__);
          goto HANDLE_ERROR;
        }

        ptr = (uint8_t *)in_surface->surfaceList[0].dataPtr + in_surface->surfaceList[0].planeParams.offset[i];
        if (is_device_memory) {
          m.srcDevice = (CUdeviceptr) ptr;
          m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        }
        else if (is_host_memory) {
          m.srcHost = (void *)ptr;
          m.srcMemoryType = CU_MEMORYTYPE_HOST;
        }

        width = GST_VIDEO_INFO_COMP_WIDTH(&(eglglessink->configured_info), i);
        height = GST_VIDEO_INFO_COMP_HEIGHT(&(eglglessink->configured_info), i);
        pstride = GST_VIDEO_INFO_COMP_PSTRIDE(&(eglglessink->configured_info), i);
        m.srcPitch = in_surface->surfaceList[0].planeParams.pitch[i];

        m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        m.dstArray = dpArray;
        m.WidthInBytes = width*pstride;
        m.Height = height;

        result = cuMemcpy2D(&m);
        if (result != CUDA_SUCCESS) {
          g_print ("cuMemcpy2D failed with error(%d) %s %d\n", result, __func__, __LINE__);
          goto HANDLE_ERROR;
        }

        result = cuGraphicsUnmapResources(1, &(eglglessink->cuResource[i]), 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsUnmapResources failed with error(%d) %s\n", result, __func__);
          goto HANDLE_ERROR;
        }

        eglglessink->stride[i] = pstride;
      }
      eglglessink->orientation =
          GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;
    }// case I420 or NV12
    break;
    default:
      g_print("buffer format not supported\n");
      return FALSE;
    break;
  } //switch

  return TRUE;

HANDLE_ERROR:
    if (eglglessink->cuResource[0])
      cuGraphicsUnmapResources(1, &(eglglessink->cuResource[0]), 0);
    if (eglglessink->cuResource[1])
      cuGraphicsUnmapResources(1, &(eglglessink->cuResource[0]), 0);
    if (eglglessink->cuResource[2])
      cuGraphicsUnmapResources(1, &(eglglessink->cuResource[0]), 0);
    return FALSE;
}


/* 更新纹理*/
static GstFlowReturn
gst_eglglessink_upload (GstEglGlesSink * eglglessink, GstBuffer * buf) {
  
  GstVideoCropMeta *crop = NULL;

  if (!buf) {
    GST_DEBUG_OBJECT (eglglessink, "Rendering previous buffer again");
  } else if (buf) {

    GstMemory *mem;

    GstVideoGLTextureUploadMeta *upload_meta;

    crop = gst_buffer_get_video_crop_meta (buf);

    upload_meta = gst_buffer_get_video_gl_texture_upload_meta (buf);

    if (gst_eglglessink_crop_changed (eglglessink, crop)) {
      if (crop) {
        eglglessink->crop.x = crop->x;
        eglglessink->crop.y = crop->y;
        eglglessink->crop.w = crop->width;
        eglglessink->crop.h = crop->height;
      } else {
        eglglessink->crop.x = 0;
        eglglessink->crop.y = 0;
        eglglessink->crop.w = eglglessink->configured_info.width;
        eglglessink->crop.h = eglglessink->configured_info.height;
      }
      eglglessink->crop_changed = TRUE;
    }

    if (upload_meta) {
      gint i;

      if (upload_meta->n_textures != (guint)eglglessink->egl_context->n_textures)
        goto HANDLE_ERROR;

      if (eglglessink->egl_context->n_textures > 3) {
        goto HANDLE_ERROR;
      }

      for (i = 0; i < eglglessink->egl_context->n_textures; i++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);
      }

      if (!gst_video_gl_texture_upload_meta_upload (upload_meta,
              eglglessink->egl_context->texture))
        goto HANDLE_ERROR;

      eglglessink->orientation = upload_meta->texture_orientation;
      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
    } else if (eglglessink->using_nvbufsurf) { /* 如果是Jetson设备会执行 */
      GstMapInfo map = { NULL, (GstMapFlags) 0, NULL, 0, 0, };
      EGLImageKHR image = EGL_NO_IMAGE_KHR;
      NvBufSurface *in_surface = NULL;

      mem = gst_buffer_peek_memory (buf, 0);

      gst_memory_map (mem, &map, GST_MAP_READ);

      /* Types of Buffers handled -
        *     NvBufSurface
        *                     - NVMM buffer type
       */
      /* NvBufSurface type are handled here */
      in_surface = (NvBufSurface*) map.data;

      GST_DEBUG_OBJECT (eglglessink, "exporting EGLImage from nvbufsurf");
      /* NvBufSurface - NVMM buffer type are handled here */
      if (in_surface->batchSize != 1) {
        GST_ERROR_OBJECT (eglglessink, "ERROR: Batch size not 1\n");
        return FALSE;
      }

      /**
       * @brief: 从一个或多个NvBufSurface缓冲区的内存中创建一个EGLImage
       *         仅支持内存类型NVBUF_MEM_SURFACE_ARRAY (只能Jetson使用)
       *         创建的EGLImage存储在 in_surface->surfaceList->mappedAddr->eglImage
       * @param surf: surf 指向NvBufSurface结构的指针，函数将所创建的EGLImage的指针存储在此结构成员中
       * @param index: 批处理中缓冲区的索引。-1指定批处理中的所有缓冲区。（上面已经判断缓冲区中只有一个）
       * @return: 成功返回0，否则返回-1。
      */
      if (NvBufSurfaceMapEglImage (in_surface, 0) !=0) {
        GST_ERROR_OBJECT (eglglessink, "ERROR: NvBufSurfaceMapEglImage\n");
        return FALSE;
      }

      /* EGLImageKHR类型 */
      image = in_surface->surfaceList[0].mappedAddr.eglImage;

      glActiveTexture (GL_TEXTURE0);
      if (got_gl_error ("glActiveTexture")) {
        goto HANDLE_ERROR;
      }
      glBindTexture (GL_TEXTURE_EXTERNAL_OES, eglglessink->egl_context->texture[0]);
      if (got_gl_error ("glBindTexture")) {
        goto HANDLE_ERROR;
      }

      GST_DEBUG_OBJECT (eglglessink, "calling glEGLImageTargetTexture2DOES");
      if (eglglessink->glEGLImageTargetTexture2DOES) {
        GST_DEBUG_OBJECT (eglglessink, "caught error in glEGLImageTargetTexture2DOES : eglImage: %p", image);
        /*  EGLImage 绑定到当前的 OpenGL ES 纹理目标上 */
        eglglessink->glEGLImageTargetTexture2DOES (GL_TEXTURE_EXTERNAL_OES, image); 
        if (got_gl_error ("glEGLImageTargetTexture2DOES")) {
          goto HANDLE_ERROR;
        }
      } else {
        GST_ERROR_OBJECT (eglglessink,
            "glEGLImageTargetTexture2DOES not supported");
        return GST_FLOW_ERROR;
      }

      eglglessink->orientation = GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;

      eglglessink->last_uploaded_buffer = buf;

      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
      GST_DEBUG_OBJECT (eglglessink, "done uploading");

      gst_memory_unmap (mem, &map);
    } else if ( gst_buffer_n_memory (buf) >= 1 && \
               (mem = gst_buffer_peek_memory (buf, 0)) && \
                gst_is_egl_image_memory (mem)) {
      guint n, i;

      n = gst_buffer_n_memory (buf);

      for (i = 0; i < n; i++) {
        mem = gst_buffer_peek_memory (buf, i);

        g_assert (gst_is_egl_image_memory (mem));

        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);

        if (eglglessink->glEGLImageTargetTexture2DOES) {
          eglglessink->glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
              gst_egl_image_memory_get_image (mem));
          if (got_gl_error ("glEGLImageTargetTexture2DOES"))
            goto HANDLE_ERROR;
        } else {
          GST_ERROR_OBJECT (eglglessink,
              "glEGLImageTargetTexture2DOES not supported");
          return GST_FLOW_ERROR;
        }

        eglglessink->orientation = gst_egl_image_memory_get_orientation (mem);
        if (eglglessink->orientation !=
            GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
            && eglglessink->orientation !=
            GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
          GST_ERROR_OBJECT (eglglessink, "Unsupported EGLImage orientation");
          return GST_FLOW_ERROR;
        }
      }

      eglglessink->last_uploaded_buffer = buf;

      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
    } else if (eglglessink->using_cuda) {
      //Handle Cuda Buffers
      if (!gst_eglglessink_cuda_buffer_copy(eglglessink, buf)) {
        goto HANDLE_ERROR;
      }
    } else {
      eglglessink->orientation =
          GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;
      if (!gst_eglglessink_fill_texture (eglglessink, buf))
        goto HANDLE_ERROR;
      eglglessink->last_uploaded_buffer = buf;
    }
  }

  return GST_FLOW_OK;

HANDLE_ERROR:
  {
    GST_ERROR_OBJECT (eglglessink, "Failed to upload texture");
    return GST_FLOW_ERROR;
  }
}

/**
 * @brief: gl顶点相关，绘制
*/
static GstFlowReturn
gst_eglglessink_render (GstEglGlesSink * eglglessink)
{
  if (eglglessink->profile)
    GstEglJitterToolAddPoint(eglglessink->pDeliveryJitter);
  
  return GST_FLOW_OK;
}


/**
 * @brief: 这个函数会在show frame之前调用，把Buffer发送到队列中，以供渲染
 * @note: 这个函数会在 GstBaseSink 的chain函数中调用
*/
static GstFlowReturn
gst_eglglessink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (bsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, GST_MINI_OBJECT_CAST (buf));
}


/**
 * @brief: 显示帧图像（但是这里并不是用这个函数去实现的）
 *         push了一个空的GstMiniObject让渲染线程去处理
*/
static GstFlowReturn
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (vsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, NULL);
}

static GstCaps *
gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstEglGlesSink *eglglessink;
  GstCaps *ret = NULL;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->sinkcaps) {
    ret = gst_caps_ref (eglglessink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }
  GST_OBJECT_UNLOCK (eglglessink);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_eglglessink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstEglGlesSink *eglglessink;

  eglglessink = GST_EGLGLESSINK (bsink);

  switch (GST_QUERY_TYPE (query)) {
#ifndef HAVE_IOS
    case GST_QUERY_CONTEXT:{
      const gchar *context_type;

      if (gst_query_parse_context_type (query, &context_type) &&
          strcmp (context_type, GST_EGL_DISPLAY_CONTEXT_TYPE) &&
          eglglessink->egl_context->display) {
        GstContext *context;

        context =
            gst_context_new_egl_display (eglglessink->egl_context->display,
            FALSE);
        gst_query_set_context (query, context);
        gst_context_unref (context);

        return TRUE;
      } else {
        return GST_BASE_SINK_CLASS (gst_eglglessink_parent_class)->query (bsink,
            query);
      }
      break;
    }
#endif
    default:
      return GST_BASE_SINK_CLASS (gst_eglglessink_parent_class)->query (bsink,
          query);
      break;
  }
}


/**
 * @brief: 设定的是 GstEglAdaptationContext->GstEGLDisplay
 */
static void
gst_eglglessink_set_context (GstElement * element, GstContext * context) {

  GstEglGlesSink *eglglessink;
  GstEGLDisplay *display = NULL;

  eglglessink = GST_EGLGLESSINK (element);

  if (gst_context_get_egl_display (context, &display)) {

    GST_OBJECT_LOCK (eglglessink);

    if (eglglessink->egl_context->set_display)
      gst_egl_display_unref (eglglessink->egl_context->set_display);
    
    /*  */
    eglglessink->egl_context->set_display = display;

    GST_OBJECT_UNLOCK (eglglessink);
  }
}

/**
 * @brief: 提议创建内存池 GstBufferPool
 */
static gboolean
gst_eglglessink_propose_allocation (GstBaseSink * bsink, GstQuery * query) {

  GstEglGlesSink *eglglessink;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  GstVideoInfo info;
  gboolean need_pool;
  guint size;
  GstAllocator *allocator;
  GstAllocationParams params;

  eglglessink = GST_EGLGLESSINK (bsink);

  /* 初始化分配器变量 */
  gst_allocation_params_init (&params);

  /* 解析查询内容 */
  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (eglglessink, "allocation query without caps");
    return FALSE;
  }

  /* 获取caps中的视频格式内容 */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (eglglessink, "allocation query with invalid caps");
    return FALSE;
  }


  GST_OBJECT_LOCK (eglglessink);
  pool = eglglessink->pool ? gst_object_ref (eglglessink->pool) : NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  /* 如果我们已经有 内存池 */
  if (pool) {
    GstCaps *pcaps;

    /* 我们检查内存池里面存储的caps格式是否跟查询的格式相等 */
    GST_DEBUG_OBJECT (eglglessink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (eglglessink, "pool has different caps");
      /* 如果内存池的caps与查询里面的caps不一样 */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  /**
   * 如果sink已经有了内存池，内存池的caps与查询的caps不相等，赋值 pool = NULL
   * 如果sink里面本身就没有内存池， poo = NULL
   */
  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (eglglessink, "allocation query has invalid caps %"
          GST_PTR_FORMAT, caps);
      return FALSE;
    }

    GST_DEBUG_OBJECT (eglglessink, "create new pool");

    /* 创建一个新的线程池对象 */
    pool = gst_egl_image_buffer_pool_new (gst_eglglessink_egl_image_buffer_pool_send_blocking,
                                          gst_object_ref (eglglessink),
                                          gst_eglglessink_egl_image_buffer_pool_on_destroy);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    /* 我们最少需要分配2个buffer，因为要保存上一帧视频 */
    gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, NULL, &params);
    if (!gst_buffer_pool_set_config (pool, config)) {
      gst_object_unref (pool);
      GST_ERROR_OBJECT (eglglessink, "failed to set pool configuration");
      return FALSE;
    }
  }

  if (pool) {
    /* 我们最少需要分配2个buffer，因为要保存上一帧视频 */
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_object_unref (pool);
  }

  /* 首先获取默认内存分配器 */
  if (!gst_egl_image_memory_is_mappable ()) { /* 因为返回FALSE，所以执行 */
    allocator = gst_allocator_find (NULL);
    /* 获取默认内存分配器中的 AllocationParam  */
    gst_query_add_allocation_param (query, allocator, &params);
    gst_object_unref (allocator);
  }

  /* 创建 GstEGLImageAllocator 内存分配器 */
  allocator = gst_egl_image_allocator_new ();
  if (!gst_egl_image_memory_is_mappable ()) /* 因为返回FALSE，所以执行 */
    params.flags |= GST_MEMORY_FLAG_NOT_MAPPABLE; 
  gst_query_add_allocation_param (query, allocator, &params);
  gst_object_unref (allocator);

  gst_query_add_allocation_meta (query, GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

/**
 * @brief: 在调用GStCaps配置函数的时候，会调用该CUDA初始化函数。
*/
static gboolean
gst_eglglessink_cuda_init (GstEglGlesSink * eglglessink) {

  CUcontext pctx;
  CUresult result;
  GLenum error;
  int i;
  guint width, height, pstride;
  GstVideoFormat videoFormat;

  cuInit(0);
  result = cuCtxCreate(&pctx, 0, 0); /* 创建CUDA上下文 */
  if (result != CUDA_SUCCESS) {
     g_print ("cuCtxCreate failed with error(%d) %s\n", result, __func__);
     return FALSE;
   }

  eglglessink->cuContext = pctx;
  eglglessink->swData = NULL;

  width = GST_VIDEO_SINK_WIDTH (eglglessink);
  height = GST_VIDEO_SINK_HEIGHT(eglglessink);

  videoFormat = eglglessink->configured_info.finfo->format;  /* 一般都是RGBA */

  switch (videoFormat) {
     case GST_VIDEO_FORMAT_BGR:
     case GST_VIDEO_FORMAT_RGB: {
         // Allocate memory for sw buffer
         eglglessink->swData = (uint8_t *)malloc(width * height * 3 * sizeof(uint8_t));
     }
     break;
     case GST_VIDEO_FORMAT_RGBA: /* 一般都是RGBA */
     case GST_VIDEO_FORMAT_BGRx: {
         glActiveTexture(GL_TEXTURE0);  /* 绑定到纹理单元0 */
         glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         error = glGetError();
         if (error != GL_NO_ERROR) {
            g_print("glerror %x error %d\n", error, __LINE__);
            return FALSE;
         }
         /**
          * @brief: 注册由@image指定的纹理，以供CUDA访问。
          * @param pCudaResource(out): 返回注册对象的句柄
          * @param image:  纹理id
          * @param target: 必须是 GL_TEXTURE_2D、GL_TEXTURE_RECTANGLE、GL_TEXTURE_CUBE_MAP、GL_TEXTURE_3D、GL_TEXTURE_2D_ARRAY 或 GL_RENDERBUFFER 中的一个。
          * @param Flags:  CU_GRAPHICS_REGISTER_FLAGS_NONE：指定对该资源的使用方式没有特殊要求。因此，假定该资源将被 CUDA 读取和写入。这是默认值。
          *                CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY：指定 CUDA 不会对此资源进行写入。
          *                CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD：指定 CUDA 不会从此资源中读取，将完全覆盖该资源的所有内容，因此之前存储在资源中的任何数据都不会被保留。
          *                CU_GRAPHICS_REGISTER_FLAGS_SURFACE_LDST：指定 CUDA 将该资源绑定到表面引用。
          *                CU_GRAPHICS_REGISTER_FLAGS_TEXTURE_GATHER：指定 CUDA 将对此资源执行纹理收集操作。
         */
         g_print ("eglglessink->egl_context->texture[0] = %d\n", eglglessink->egl_context->texture[0]);
         result = cuGraphicsGLRegisterImage(&(eglglessink->cuResource[0]), eglglessink->egl_context->texture[0], GL_TEXTURE_2D, 0);
         if (result != CUDA_SUCCESS) {
            g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, eglglessink->egl_context->texture[0]);
            return FALSE;
         }
     }
     break;
     case GST_VIDEO_FORMAT_I420: {
         for (i = 0; i < 3; i++) {
            if (i == 0)
              glActiveTexture (GL_TEXTURE0);
            else if (i == 1)
              glActiveTexture (GL_TEXTURE1);
            else if (i == 2)
              glActiveTexture (GL_TEXTURE2);

            width = GST_VIDEO_INFO_COMP_WIDTH(&(eglglessink->configured_info), i);
            height = GST_VIDEO_INFO_COMP_HEIGHT(&(eglglessink->configured_info), i);

            glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            error = glGetError();
            if (error != GL_NO_ERROR) {
              g_print("glerror %x error %d\n", error, __LINE__);
              return FALSE;
            }
            result = cuGraphicsGLRegisterImage(&(eglglessink->cuResource[i]), eglglessink->egl_context->texture[i], GL_TEXTURE_2D, 0);
            if (result != CUDA_SUCCESS) {
               g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, eglglessink->egl_context->texture[i]);
               return FALSE;
            }
         }
     }
     break;
     case GST_VIDEO_FORMAT_NV12: {
         for (i = 0; i < 2; i++) {
            if (i == 0)
              glActiveTexture (GL_TEXTURE0);
            else if (i == 1)
              glActiveTexture (GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);

            width = GST_VIDEO_INFO_COMP_WIDTH(&(eglglessink->configured_info), i);
            height = GST_VIDEO_INFO_COMP_HEIGHT(&(eglglessink->configured_info), i);
            pstride = GST_VIDEO_INFO_COMP_PSTRIDE(&(eglglessink->configured_info), i);

            if (i == 0)
              glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width*pstride, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
            else if ( i == 1)
              glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width*pstride, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            error = glGetError();
            if (error != GL_NO_ERROR) {
              g_print("glerror %x error %d\n", error, __LINE__);
              return FALSE;
            }
            result = cuGraphicsGLRegisterImage(&(eglglessink->cuResource[i]), eglglessink->egl_context->texture[i], GL_TEXTURE_2D, 0);
            if (result != CUDA_SUCCESS) {
               g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, eglglessink->egl_context->texture[i]);
               return FALSE;
            }
         }
     }
     break;
     default:
         g_print("buffer format not supported\n");
         return FALSE;
  }
  return TRUE;
}

static void
gst_eglglessink_cuda_cleanup (GstEglGlesSink * eglglessink)
{
  CUresult result;
  guint i;

  for (i = 0; i < 3; i++) {
    if (eglglessink->cuResource[i])
      /* 删除一个由 CUDA 访问的图形资源。 */
      cuGraphicsUnregisterResource (eglglessink->cuResource[i]);
  }

  if (eglglessink->cuContext) {
    result = cuCtxDestroy(eglglessink->cuContext);
    if (result != CUDA_SUCCESS) {
      g_print ("cuCtxDestroy failed with error(%d) %s\n", result, __func__);
    }
  }

  // Free sw buffer memory
  if (eglglessink->swData) {
    free(eglglessink->swData);
  }
}

/**
 * @brief: 设定当前线程egl上下文
 * 
*/
static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps)
{
  gboolean ret = TRUE;
  GstVideoInfo info;
  GdkGLContext *context = gtk_gst_paintable_get_context (GTK_GST_PAINTABLE(eglglessink->paintable));

  gst_video_info_init (&info);
  if (!(ret = gst_video_info_from_caps (&info, caps))) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't parse caps");
    goto HANDLE_ERROR;
  }

  eglglessink->configured_info = info;
  GST_VIDEO_SINK_WIDTH (eglglessink) = info.width;
  GST_VIDEO_SINK_HEIGHT (eglglessink) = info.height;

  /* caps 协商 */
  if (eglglessink->configured_caps) {
    GST_DEBUG_OBJECT (eglglessink, "Caps were already set");
    if (gst_caps_can_intersect (caps, eglglessink->configured_caps)) {
      GST_DEBUG_OBJECT (eglglessink, "Caps are compatible anyway");
      goto SUCCEED;
    }

    GST_DEBUG_OBJECT (eglglessink, "Caps are not compatible, reconfiguring");

    /* EGL/GLES cleanup */
    if (eglglessink->using_cuda) {
      gst_eglglessink_cuda_cleanup(eglglessink);
    }
    // gst_egl_adaptation_cleanup (eglglessink->egl_context);
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  /* 把GdkGLContext中EGLContext设置为当前线程的egl上下文 */
  gdk_gl_context_make_current (context);

  eglglessink->egl_context->egl_context = eglGetCurrentContext ();

  gst_caps_replace (&eglglessink->configured_caps, caps);

  /* gl纹理创建CUDA访问句柄 */
  if (eglglessink->using_cuda) {
    if (!gst_eglglessink_cuda_init(eglglessink)) {
       GST_ERROR_OBJECT (eglglessink, "Cuda Init failed");
       goto HANDLE_ERROR;
    }
  }

SUCCEED:
  GST_INFO_OBJECT (eglglessink, "Configured caps successfully");
  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Configuring caps failed");
  return FALSE;
}

static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps) {

  GstEglGlesSink *eglglessink;
  GstVideoInfo info;
  GstCapsFeatures *features;
  GstBufferPool *newpool, *oldpool;
  GstStructure *config;
  GstAllocationParams params = { 0, };

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_DEBUG_OBJECT (eglglessink,
      "Current caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglglessink->current_caps, caps);

  features = gst_caps_get_features(caps, 0);
  if (gst_caps_features_contains(features, "memory:NVMM")) {
#if defined(NVOS_IS_L4T)
    eglglessink->using_nvbufsurf = TRUE;  /* Jetson */
#else
    eglglessink->using_cuda = TRUE; /* GPU CUDA */
#endif
  }

  if (gst_eglglessink_queue_object (eglglessink,
          GST_MINI_OBJECT_CAST (caps)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (eglglessink, "Failed to configure caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (eglglessink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  /* 如果是 Jetson 设备，则执行 */
  if (!eglglessink->using_cuda) {
  newpool = gst_egl_image_buffer_pool_new( gst_eglglessink_egl_image_buffer_pool_send_blocking,
                                           gst_object_ref (eglglessink),
                                           gst_eglglessink_egl_image_buffer_pool_on_destroy);
  config = gst_buffer_pool_get_config (newpool);
  
  /*我们至少需要2个缓冲区，因为我们保留了最后一个*/
  gst_buffer_pool_config_set_params (config, caps, info.size, 2, 0);
  gst_buffer_pool_config_set_allocator (config, NULL, &params);
  if (!gst_buffer_pool_set_config (newpool, config)) {
    gst_object_unref (newpool);
    GST_ERROR_OBJECT (eglglessink, "Failed to set buffer pool configuration");
    return FALSE;
  }

  GST_OBJECT_LOCK (eglglessink);
  oldpool = eglglessink->pool;
  eglglessink->pool = newpool;
  GST_OBJECT_UNLOCK (eglglessink);

  if (oldpool)
    gst_object_unref (oldpool);
  }

  gst_caps_replace (&eglglessink->current_caps, caps);

  return TRUE;
}


/**
 * @brief: 当元素@eglglessink 从 NULL_TO_READY 状态的时候
 *         
*/
static gboolean
gst_eglglessink_open (GstEglGlesSink * eglglessink) {

  if (!egl_init (eglglessink)) {
    return FALSE;
  }

  if (eglglessink->profile) {
    eglglessink->pDeliveryJitter = GstEglAllocJitterTool("frame delivery", 100);
    GstEglJitterToolSetShow(eglglessink->pDeliveryJitter, 0 /*eglglessink->profile*/);
  }

  return TRUE;
}

/**
 * @brief: 当元素@eglglessink 从 READY_TO_NULL 状态的时候会调用该函数
*/
static gboolean
gst_eglglessink_close (GstEglGlesSink * eglglessink)
{
  double fJitterAvg = 0, fJitterStd = 0, fJitterHighest = 0;

  g_mutex_lock (&eglglessink->render_lock);
  eglglessink->is_closing = TRUE;
  g_mutex_unlock (&eglglessink->render_lock);
  g_cond_broadcast (&eglglessink->render_exit_cond);

  if (eglglessink->thread) {
    g_thread_join (eglglessink->thread);
    eglglessink->thread = NULL;
  }

  if (eglglessink->egl_context->display) {
    gst_egl_display_unref (eglglessink->egl_context->display);
    eglglessink->egl_context->display = NULL;
  }

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->pool)
    gst_object_unref (eglglessink->pool);
  eglglessink->pool = NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  if (eglglessink->profile) {
    GstEglJitterToolGetAvgs(eglglessink->pDeliveryJitter, &fJitterStd, &fJitterAvg, &fJitterHighest);
    printf("\n");
    printf("--------Jitter Statistics------------");
    printf("--------Average jitter = %f uSec \n", fJitterStd);
    printf("--------Highest instantaneous jitter = %f uSec \n", fJitterHighest);
    printf("--------Mean time between frame(used in jitter) = %f uSec \n", fJitterAvg);
    printf("\n");

    GstEglFreeJitterTool(eglglessink->pDeliveryJitter);
    eglglessink->pDeliveryJitter = NULL;
  }

  gst_caps_unref (eglglessink->sinkcaps);
  eglglessink->sinkcaps = NULL;
  eglglessink->egl_started = FALSE;

  if (GST_OBJECT_REFCOUNT(eglglessink))
    gst_object_unref (eglglessink);
  return TRUE;
}

static GstStateChangeReturn
gst_eglglessink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglGlesSink *eglglessink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglglessink = GST_EGLGLESSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_eglglessink_open (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_eglglessink_start (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_eglglessink_close (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_eglglessink_stop (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglglessink_finalize (GObject * object)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  if (eglglessink->queue)
    g_object_unref (eglglessink->queue);
  eglglessink->queue = NULL;

  g_mutex_clear (&eglglessink->window_lock);
  g_cond_clear (&eglglessink->render_cond);
  g_cond_clear (&eglglessink->render_exit_cond);
  g_mutex_clear (&eglglessink->render_lock);

  gst_egl_adaptation_context_free (eglglessink->egl_context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      eglglessink->create_window = g_value_get_boolean (value);
      break;
    case PROP_DISPLAY:
      eglglessink->display = g_value_get_pointer (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglglessink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_WINDOW_X:
      eglglessink->window_x = g_value_get_uint (value);
      break;
    case PROP_WINDOW_Y:
      eglglessink->window_y = g_value_get_uint (value);
      break;
    case PROP_WINDOW_WIDTH:
      eglglessink->window_width = g_value_get_uint (value);
      break;
    case PROP_WINDOW_HEIGHT:
      eglglessink->window_height = g_value_get_uint (value);
      break;
    case PROP_PROFILE:
      eglglessink->profile = g_value_get_uint (value);
      break;
    case PROP_WINSYS:
      eglglessink->winsys = g_strdup (g_value_get_string(value));
      break;
    case PROP_SHOW_LATENCY:
      eglglessink->show_latency = g_value_get_boolean (value);
      break;
    case PROP_ROWS:
      eglglessink->rows = g_value_get_uint (value);
      eglglessink->change_port = -1;
      break;
    case PROP_COLUMNS:
      eglglessink->columns = g_value_get_uint (value);
      eglglessink->change_port = -1;
      break;
#ifdef IS_DESKTOP
    case PROP_GPU_DEVICE_ID:
      eglglessink->gpu_id = g_value_get_uint (value);
      break;
#endif
    case PROP_NVBUF_API_VERSION:
      eglglessink->nvbuf_api_version_new = g_value_get_boolean (value);
      break;
    case PROP_IVI_SURF_ID:
      eglglessink->ivisurf_id = g_value_get_uint (value);
      break;
    /* 自定义属性 */

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglglessink->create_window);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglglessink->force_aspect_ratio);
      break;
    case PROP_DISPLAY:
      g_value_set_pointer (value, eglglessink->display);
      break;
    case PROP_WINDOW_X:
      g_value_set_uint (value, eglglessink->window_x);
      break;
    case PROP_WINDOW_Y:
      g_value_set_uint (value, eglglessink->window_y);
      break;
    case PROP_WINDOW_WIDTH:
      g_value_set_uint (value, eglglessink->window_width);
      break;
    case PROP_WINDOW_HEIGHT:
      g_value_set_uint (value, eglglessink->window_height);
      break;
    case PROP_PROFILE:
      g_value_set_uint (value, eglglessink->profile);
      break;
    case PROP_WINSYS:
      g_value_set_string (value, eglglessink->winsys);
      break;
    case PROP_SHOW_LATENCY:
      g_value_set_boolean (value, eglglessink->show_latency);
      break;
    case PROP_ROWS:
      g_value_set_uint (value, eglglessink->rows);
      break;
    case PROP_COLUMNS:
      g_value_set_uint (value, eglglessink->columns);
      break;
#ifdef IS_DESKTOP
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, eglglessink->gpu_id);
      break;
#endif
    case PROP_NVBUF_API_VERSION:
      g_value_set_boolean (value, eglglessink->nvbuf_api_version_new);
      break;
    case PROP_IVI_SURF_ID:
      g_value_set_uint (value, eglglessink->ivisurf_id);
      break;
    
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_eglglessink_class_init (GstEglGlesSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglglessink_set_property;
  gobject_class->get_property = gst_eglglessink_get_property;
  gobject_class->finalize = gst_eglglessink_finalize;

  gstelement_class->change_state = gst_eglglessink_change_state;
  gstelement_class->set_context = gst_eglglessink_set_context; /* gst_element_set_context 函数调用 */

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps);
  gstbasesink_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_eglglessink_propose_allocation); /* 进行内存池BufferPool的创建 */
  gstbasesink_class->prepare = GST_DEBUG_FUNCPTR (gst_eglglessink_prepare);  /* 显示帧先调用该函数 */
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_eglglessink_query);

  gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame); /* 显示帧再调用该函数 */

  g_object_class_install_property (gobject_class, PROP_WINSYS,
      g_param_spec_string ("winsys", "Windowing System",
          "Takes in strings \"x11\" or \"wayland\" to specify the windowing system to be used",
          "x11", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHOW_LATENCY,
      g_param_spec_boolean ("show-latency", "Show Latency",
          "To print the latency between eglSwapbuffer and Display HW flip. "
          "This property is only avaialbe in wayland. ",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "If set to true, the sink will attempt to create it's own window to "
          "render to if none is provided. This is currently only supported "
          "when the sink is used under X11",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Respect aspect ratio when scaling",
          "If set to true, the sink will attempt to preserve the incoming "
          "frame's geometry while scaling, taking both the storage's and "
          "display's pixel aspect ratio into account",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_pointer ("display",
          "Set X Display to be used",
          "If set, the sink will use the passed X Display for rendering",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_X,
      g_param_spec_uint ("window-x",
          "Window x coordinate",
          "X coordinate of window", 0, G_MAXINT, 10,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_Y,
      g_param_spec_uint ("window-y",
          "Window y coordinate",
          "Y coordinate of window", 0, G_MAXINT, 10,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_WIDTH,
      g_param_spec_uint ("window-width",
          "Window width",
          "Width of window", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_HEIGHT,
      g_param_spec_uint ("window-height",
          "Window height",
          "Height of window", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_uint ("profile",
          "profile",
          "gsteglglessink jitter information", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROWS,
        g_param_spec_uint ("rows",
            "Display rows",
            "Rows of Display", 1, G_MAXINT, 1,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COLUMNS,
        g_param_spec_uint ("columns",
            "Display columns",
            "Columns of display", 1, G_MAXINT, 1,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifdef IS_DESKTOP
  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id", "Set GPU Device ID",
          "Set GPU Device ID",
          0, G_MAXUINT, DEFAULT_GPU_ID,
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));
#endif

  // export GST_PLUGIN_PATH=/home/lieryang/Desktop/gstegl_src/gst-egl

  g_object_class_install_property (gobject_class, PROP_IVI_SURF_ID,
      g_param_spec_uint ("ivisurf-id", "Wayland IVI surface ID",
          "Set Wayland IVI surface ID, only available for Wayland IVI shell",
          0, G_MAXUINT, 0,
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

  signals[PAINTABLE] =
    g_signal_new ("paintable",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, GDK_TYPE_PAINTABLE);
  
  gst_element_class_set_static_metadata (gstelement_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the VideoOverlay interface",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_eglglessink_sink_template_factory));
  g_object_class_install_property (gobject_class, PROP_NVBUF_API_VERSION,
      g_param_spec_boolean ("bufapi-version",
          "Use new buf API",
          "Set to use new buf API",
          DEFAULT_NVBUF_API_VERSION_NEW, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static gboolean
queue_check_full_func (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  return visible != 0;
}

static void
gst_eglglessink_init (GstEglGlesSink * eglglessink)
{
  eglglessink->egl_context =
      gst_egl_adaptation_context_new (GST_ELEMENT_CAST (eglglessink));

  /* Init defaults */

  /** Flags */
  eglglessink->have_window = FALSE;
  eglglessink->egl_context->have_surface = FALSE;
  eglglessink->egl_context->have_vbo = FALSE;
  eglglessink->egl_context->have_texture = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->using_own_window = FALSE;
  eglglessink->using_cuda = FALSE;
  eglglessink->using_nvbufsurf = FALSE;
  eglglessink->nvbuf_api_version_new = DEFAULT_NVBUF_API_VERSION_NEW;

  /** Props */
  g_mutex_init (&eglglessink->window_lock);
  eglglessink->create_window = TRUE;
  eglglessink->display = EGL_NO_DISPLAY;
  eglglessink->force_aspect_ratio = TRUE;
  eglglessink->winsys = "x11";

  g_mutex_init (&eglglessink->render_lock);
  g_cond_init (&eglglessink->render_cond);
  g_cond_init (&eglglessink->render_exit_cond);
  eglglessink->queue =
      gst_data_queue_new (queue_check_full_func, NULL, NULL, NULL);
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  eglglessink->render_region.x = 0;
  eglglessink->render_region.y = 0;
  eglglessink->render_region.w = -1;
  eglglessink->render_region.h = -1;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = FALSE;
  eglglessink->window_x = 10;
  eglglessink->window_y = 10;
  eglglessink->window_width = 0;
  eglglessink->window_height = 0;
  eglglessink->profile = 0;
  eglglessink->rows = 1;
  eglglessink->columns = 1;
  eglglessink->cuContext = NULL;
  eglglessink->cuResource[0] = NULL;
  eglglessink->cuResource[1] = NULL;
  eglglessink->cuResource[2] = NULL;
  eglglessink->gpu_id = 0;

}


static GstBufferPool *
gst_egl_image_buffer_pool_new (GstEGLImageBufferPoolSendBlockingAllocate blocking_allocate_func, 
                               gpointer blocking_allocate_data,
                               GDestroyNotify destroy_func) {

  GstEGLImageBufferPool *pool;

  pool = g_object_new (gst_egl_image_buffer_pool_get_type (), NULL);
  pool->last_buffer = NULL;
  pool->send_blocking_allocate_func = blocking_allocate_func;
  pool->send_blocking_allocate_data = blocking_allocate_data;
  pool->send_blocking_allocate_destroy = destroy_func;

  return (GstBufferPool *) pool;
}


/**
 * 初始化插件的入口点，初始化插件本身，注册元素工厂和其他功能
*/
static gboolean
#ifdef IS_DESKTOP
nvdsgst_eglglessink_plugin_init (GstPlugin * plugin)
#else
eglglessink_plugin_init (GstPlugin * plugin)
#endif
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglglessink_debug, "nveglglessink",
      0, "Simple EGL/GLES Sink");

  gst_egl_adaption_init ();

  return gst_element_register (plugin, "vpfeglglessink", GST_RANK_SECONDARY,
      GST_TYPE_EGLGLESSINK);
}

/**
 * gstreamer 寻找此结构以注册 eglglessinks
*/
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef IS_DESKTOP
    vpfeglglessink,
#else
    nveglglessink,
#endif
    "EGL/GLES sink",
#ifdef IS_DESKTOP
    nvdsgst_eglglessink_plugin_init,
    VERSION,
#else
    eglglessink_plugin_init,
    VERSION,
#endif
    GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
