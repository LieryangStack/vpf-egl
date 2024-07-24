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
#include <libdrm/drm_fourcc.h>

GdkTexture *dmabuf_texture = NULL;

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
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:NVMM",
            "{ " "RGBA }")
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
  PROP_PROFILE,
  PROP_NVBUF_API_VERSION,
#ifdef IS_DESKTOP
  PROP_GPU_DEVICE_ID,
#endif
};

static void 
gst_eglglessink_finalize (GObject * object);
static void 
gst_eglglessink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void 
gst_eglglessink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn 
gst_eglglessink_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn 
gst_eglglessink_prepare (GstBaseSink * bsink, GstBuffer * buf);
static GstFlowReturn 
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf);
static gboolean 
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps 
*gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter);
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
 * @brief: 处理 GstBuffer，然后进行渲染
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
    } else if (GST_IS_BUFFER (object)) { /* 如果接收到 GstBuffer  */
      GstBuffer *buf = GST_BUFFER_CAST (item->object);

      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_upload (eglglessink, buf); /* 将GPU内部的纹理更新到我们创建的纹理 eglglessink->egl_context->texture[0] */
        if (last_flow == GST_FLOW_OK)
            gdk_paintable_invalidate_contents (eglglessink->paintable);
              // g_main_context_invoke_full (NULL,
              //                 G_PRIORITY_DEFAULT,
              //                 gtk_gst_paintable_set_texture_invoke,
              //                 eglglessink, NULL);
          // g_signal_emit (eglglessink, signals[UI_RENDER], 0);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else if (!object) {  /* 如果是 object == NULL */
      if (eglglessink->configured_caps) {
        
        last_flow = gst_eglglessink_render (eglglessink);  /* 绘制OpenGL ES顶点 */

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

  if (eglglessink->configured_caps) {
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

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

  eglglessink->last_flow = GST_FLOW_OK;;
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
     } // case RGBA
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

  if (!buf) {
    GST_DEBUG_OBJECT (eglglessink, "Rendering previous buffer again");
  } else if (buf) {

    GstMemory *mem;

    if (eglglessink->using_nvbufsurf) { /* 如果是Jetson设备会执行 */
      GstMapInfo map = { NULL, (GstMapFlags) 0, NULL, 0, 0, };
      NvBufSurface *in_surface = NULL;

      mem = gst_buffer_peek_memory (buf, 0);

      gst_memory_map (mem, &map, GST_MAP_READ);

      in_surface = (NvBufSurface*) map.data;

      NvBufSurfaceMapParams params;
      NvBufSurfaceGetMapParams (in_surface, 0, &params);

      GdkDmabufTextureBuilder *builder = gdk_dmabuf_texture_builder_new ();
      gdk_dmabuf_texture_builder_set_display (builder, gdk_display_get_default());
      gdk_dmabuf_texture_builder_set_fourcc (builder, DRM_FORMAT_ABGR8888);
      gdk_dmabuf_texture_builder_set_modifier (builder, 0);
      gdk_dmabuf_texture_builder_set_width (builder, params.planes[0].width);
      gdk_dmabuf_texture_builder_set_height (builder, params.planes[0].height);
      gdk_dmabuf_texture_builder_set_n_planes (builder, params.num_planes);
      
      gdk_dmabuf_texture_builder_set_fd (builder, 0, params.fd);
      gdk_dmabuf_texture_builder_set_offset (builder, 0, params.planes[0].offset);
      gdk_dmabuf_texture_builder_set_stride (builder, 0, params.planes[0].pitch);

      GError *error = NULL;
      dmabuf_texture = gdk_dmabuf_texture_builder_build (builder, NULL, NULL, &error);
      if (error) {
        g_print ("dmabuf build texture error\n");
        g_clear_error (&error);
      }

      g_object_unref (builder);

      GST_DEBUG_OBJECT (eglglessink, "exporting EGLImage from nvbufsurf");
      /* NvBufSurface - NVMM buffer type are handled here */
      if (in_surface->batchSize != 1) {
        GST_ERROR_OBJECT (eglglessink, "ERROR: Batch size not 1\n");
        return FALSE;
      }

      eglglessink->last_uploaded_buffer = buf;

      GST_DEBUG_OBJECT (eglglessink, "done uploading");

      gst_memory_unmap (mem, &map);
    } else if (eglglessink->using_cuda) {  /* dGPU CUDA 更新纹理 */
      if (!gst_eglglessink_cuda_buffer_copy(eglglessink, buf)) {
        goto HANDLE_ERROR;
      }
    } else {
      GST_ERROR_OBJECT (eglglessink, "Not Jetson or dGPU device!!!");
      goto HANDLE_ERROR;
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
  switch (GST_QUERY_TYPE (query)) {
    default:
      return GST_BASE_SINK_CLASS (gst_eglglessink_parent_class)->query (bsink,
          query);
      break;
  }
}


/**
 * @brief: 在调用GStCaps配置函数的时候，会调用该CUDA初始化函数。
*/
static gboolean
gst_eglglessink_cuda_init (GstEglGlesSink * eglglessink) {

  CUcontext pctx;
  CUresult result;
  GLenum error;
  guint width, height;
  GstVideoFormat videoFormat;

  cuInit(0);
  result = cuCtxCreate(&pctx, 0, 0); /* 创建CUDA上下文 */
  if (result != CUDA_SUCCESS) {
     g_print ("cuCtxCreate failed with error(%d) %s\n", result, __func__);
     return FALSE;
   }

  eglglessink->cuContext = pctx;

  width = GST_VIDEO_SINK_WIDTH (eglglessink);
  height = GST_VIDEO_SINK_HEIGHT(eglglessink);

  videoFormat = eglglessink->configured_info.finfo->format;  /* 一般都是RGBA */

  switch (videoFormat) {
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

  /* 得到视频格式信息 */
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

/**
 * @brief: 设定sink pad的caps
 */
static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps) {

  GstEglGlesSink *eglglessink;
  GstVideoInfo info;
  GstCapsFeatures *features;

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
    case PROP_PROFILE:
      eglglessink->profile = g_value_get_uint (value);
      break;
    case PROP_NVBUF_API_VERSION:
      eglglessink->nvbuf_api_version_new = g_value_get_boolean (value);
      break;
#ifdef IS_DESKTOP
    case PROP_GPU_DEVICE_ID:
      eglglessink->gpu_id = g_value_get_uint (value);
      break;
#endif
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
    case PROP_PROFILE:
      g_value_set_uint (value, eglglessink->profile);
      break;
    case PROP_NVBUF_API_VERSION:
      g_value_set_boolean (value, eglglessink->nvbuf_api_version_new);
      break;
#ifdef IS_DESKTOP
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, eglglessink->gpu_id);
      break;
#endif
    
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

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps); /* 设定sink pad的caps */
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps); /* 得到sink pad支持的cpas */
  gstbasesink_class->prepare = GST_DEBUG_FUNCPTR (gst_eglglessink_prepare);  /* 显示帧先调用该函数 */
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_eglglessink_query);

  gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame); /* 显示帧再调用该函数 */

 
  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_uint ("profile",
          "profile",
          "gsteglglessink jitter information", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifdef IS_DESKTOP
  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id", "Set GPU Device ID",
          "Set GPU Device ID",
          0, G_MAXUINT, DEFAULT_GPU_ID,
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));
#endif
 g_object_class_install_property (gobject_class, PROP_NVBUF_API_VERSION,
      g_param_spec_boolean ("bufapi-version",
          "Use new buf API",
          "Set to use new buf API",
          DEFAULT_NVBUF_API_VERSION_NEW, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  eglglessink->egl_context->have_surface = FALSE;
  eglglessink->egl_context->have_vbo = FALSE;
  eglglessink->egl_context->have_texture = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->using_cuda = FALSE; /* dGPU CUDA */
  eglglessink->using_nvbufsurf = FALSE; /* Jetson */
  eglglessink->nvbuf_api_version_new = DEFAULT_NVBUF_API_VERSION_NEW;

  g_mutex_init (&eglglessink->render_lock);
  g_cond_init (&eglglessink->render_cond);
  g_cond_init (&eglglessink->render_exit_cond);
  eglglessink->queue =
      gst_data_queue_new (queue_check_full_func, NULL, NULL, NULL);
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  eglglessink->profile = 0;
  eglglessink->cuContext = NULL;
  eglglessink->cuResource[0] = NULL;
  eglglessink->cuResource[1] = NULL;
  eglglessink->cuResource[2] = NULL;
  eglglessink->gpu_id = 0;
}


/**
 * 初始化插件的入口点，初始化插件本身，注册元素工厂和其他功能
*/
static gboolean
vpfeglglessink_plugin_init (GstPlugin * plugin) {
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
    vpfeglglessink,
    "EGL/GLES sink",
    vpfeglglessink_plugin_init,
    VERSION,
    GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
